#include "core/platform/windows_wasapi_graph_runner.h"

#include <algorithm>
#include <utility>

namespace sar::platform {

namespace {

std::vector<WasapiStreamError> validate_graph_shape(
    const graph::Graph& graph,
    const realtime::AudioBuffer& input,
    const realtime::AudioBuffer& output) {
  if (graph.node_count() <= 1) {
    return {};
  }

  const auto required_channels = std::max(input.channels(), output.channels());
  const auto required_frames = std::max(input.frames(), output.frames());
  if (graph.channels() >= required_channels && graph.frames() >= required_frames) {
    return {};
  }

  return {
      {
          "graph_buffer_too_small",
          "Graph scratch buffers must cover the runner input and output shapes.",
      },
  };
}

std::vector<WasapiStreamError> validate_graph_sample_rate(
    const graph::Graph& graph,
    const WindowsWasapiStream* capture_stream,
    const WindowsWasapiStream* render_stream) {
  if (capture_stream != nullptr &&
      graph.sample_rate() != capture_stream->probe().mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI capture stream sample rate.",
        },
    };
  }

  if (render_stream != nullptr &&
      graph.sample_rate() != render_stream->probe().mix_format.sample_rate) {
    return {
        {
            "graph_sample_rate_mismatch",
            "Graph sample rate must match the WASAPI render stream sample rate.",
        },
    };
  }

  return {};
}

}  // namespace

WasapiGraphRunnerResult WasapiGraphRunnerResult::success(WasapiGraphRunnerStats stats) {
  return {stats, {}};
}

WasapiGraphRunnerResult WasapiGraphRunnerResult::failure(
    std::vector<WasapiStreamError> errors) {
  return {{}, std::move(errors)};
}

bool WasapiGraphRunnerResult::ok() const noexcept {
  return errors_.empty();
}

const WasapiGraphRunnerStats& WasapiGraphRunnerResult::stats() const noexcept {
  return stats_;
}

const std::vector<WasapiStreamError>& WasapiGraphRunnerResult::errors() const noexcept {
  return errors_;
}

WasapiGraphRunnerResult::WasapiGraphRunnerResult(WasapiGraphRunnerStats stats,
                                                 std::vector<WasapiStreamError> errors)
    : stats_(stats), errors_(std::move(errors)) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WindowsWasapiStream* capture_stream,
                                                   WindowsWasapiStream* render_stream,
                                                   std::size_t channels,
                                                   std::size_t frames)
    : WindowsWasapiGraphRunner(capture_stream,
                               render_stream,
                               channels,
                               frames,
                               channels,
                               frames) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WindowsWasapiStream* capture_stream,
                                                   WindowsWasapiStream* render_stream,
                                                   std::size_t capture_channels,
                                                   std::size_t capture_frames,
                                                   std::size_t render_channels,
                                                   std::size_t render_frames)
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      input_(capture_channels, capture_frames),
      output_(render_channels, render_frames) {}

realtime::AudioBuffer& WindowsWasapiGraphRunner::input_buffer() noexcept {
  return input_;
}

const realtime::AudioBuffer& WindowsWasapiGraphRunner::input_buffer() const noexcept {
  return input_;
}

realtime::AudioBuffer& WindowsWasapiGraphRunner::output_buffer() noexcept {
  return output_;
}

const realtime::AudioBuffer& WindowsWasapiGraphRunner::output_buffer() const noexcept {
  return output_;
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::start_streams() noexcept {
  if (capture_stream_ != nullptr) {
    auto result = capture_stream_->start();
    if (!result.ok()) {
      return WasapiGraphRunnerResult::failure(result.errors());
    }
  }

  if (render_stream_ != nullptr) {
    auto result = render_stream_->start();
    if (!result.ok()) {
      if (capture_stream_ != nullptr) {
        static_cast<void>(capture_stream_->stop());
      }
      return WasapiGraphRunnerResult::failure(result.errors());
    }
  }

  return WasapiGraphRunnerResult::success({});
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::stop_streams() noexcept {
  std::vector<WasapiStreamError> errors;

  if (render_stream_ != nullptr) {
    auto result = render_stream_->stop();
    if (!result.ok()) {
      errors.insert(errors.end(), result.errors().begin(), result.errors().end());
    }
  }

  if (capture_stream_ != nullptr) {
    auto result = capture_stream_->stop();
    if (!result.ok()) {
      errors.insert(errors.end(), result.errors().begin(), result.errors().end());
    }
  }

  if (!errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(errors));
  }

  return WasapiGraphRunnerResult::success({});
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms,
    void* cancellation_event) noexcept {
  WasapiGraphRunnerStats stats;

  auto sample_rate_errors =
      validate_graph_sample_rate(graph, capture_stream_, render_stream_);
  if (!sample_rate_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(sample_rate_errors));
  }

  if (capture_stream_ != nullptr) {
    input_.clear();
    auto capture_result =
        capture_stream_->capture_once(input_, timeout_ms, cancellation_event);
    if (!capture_result.ok()) {
      return WasapiGraphRunnerResult::failure(capture_result.errors());
    }
    if (capture_result.cancelled()) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    stats.captured_frames = capture_result.frames();
    stats.capture_silent = capture_result.silent();
    stats.capture_data_discontinuity = capture_result.data_discontinuity();
    stats.capture_timestamp_error = capture_result.timestamp_error();
    if (stats.capture_data_discontinuity) {
      diagnostics.xrun_count += 1;
    }
    if (stats.capture_silent) {
      stats.capture_silent_frames = stats.captured_frames;
    }
    stats.capture_partial =
        stats.captured_frames > 0 && stats.captured_frames < input_.frames();
    if (stats.capture_partial) {
      stats.capture_partial_frames =
          static_cast<std::uint32_t>(input_.frames() - stats.captured_frames);
    }
    if (stats.captured_frames == 0) {
      stats.capture_stream_idle = true;
      stats.capture_wait_timed_out = capture_result.timed_out();
      return WasapiGraphRunnerResult::success(stats);
    }
  }

  auto shape_errors = validate_graph_shape(graph, input_, output_);
  if (!shape_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(shape_errors));
  }

  output_.clear();
  graph.process(input_, output_, diagnostics);
  stats.graph_processed = true;

  if (render_stream_ != nullptr) {
    auto render_result =
        render_stream_->render_once(output_, timeout_ms, cancellation_event);
    if (!render_result.ok()) {
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    if (render_result.cancelled()) {
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    stats.rendered_frames = render_result.frames();
    stats.render_stream_idle = stats.rendered_frames == 0;
    stats.render_wait_timed_out = render_result.timed_out();
    stats.render_partial =
        stats.rendered_frames > 0 && stats.rendered_frames < output_.frames();
    if (stats.render_partial) {
      stats.render_partial_frames =
          static_cast<std::uint32_t>(output_.frames() - stats.rendered_frames);
    }
  }

  return WasapiGraphRunnerResult::success(stats);
}

}  // namespace sar::platform
