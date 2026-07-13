#include "core/platform/windows_wasapi_graph_runner.h"

#include <algorithm>
#include <stdexcept>
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
    const WasapiStreamIo* capture_stream,
    const WasapiStreamIo* render_stream) {
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

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                                                   WasapiStreamIo* render_stream,
                                                   std::size_t channels,
                                                   std::size_t frames)
    : WindowsWasapiGraphRunner(capture_stream,
                               render_stream,
                               channels,
                               frames,
                               channels,
                               frames) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(WasapiStreamIo* capture_stream,
                                                   WasapiStreamIo* render_stream,
                                                   std::size_t capture_channels,
                                                   std::size_t capture_frames,
                                                   std::size_t render_channels,
                                                   std::size_t render_frames)
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      input_(capture_channels, capture_frames),
      output_(render_channels, render_frames) {}

WindowsWasapiGraphRunner::BufferedPath::BufferedPath(
    std::size_t channels,
    std::size_t packet_frames,
    std::size_t fifo_frames)
    : packet(channels, packet_frames), fifo(channels, fifo_frames) {}

WindowsWasapiGraphRunner::WindowsWasapiGraphRunner(
    WasapiStreamIo* capture_stream,
    WasapiStreamIo* render_stream,
    std::size_t input_channels,
    std::size_t output_channels,
    std::size_t graph_block_frames,
    std::size_t capture_packet_capacity_frames,
    std::size_t render_packet_capacity_frames,
    std::size_t fifo_capacity_frames,
    bool prime_render_silence)
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      input_(input_channels, graph_block_frames),
      output_(output_channels, graph_block_frames),
      graph_block_frames_(graph_block_frames),
      render_master_(prime_render_silence) {
  if (fifo_capacity_frames < graph_block_frames ||
      (capture_stream != nullptr && capture_packet_capacity_frames == 0) ||
      (render_stream != nullptr && render_packet_capacity_frames == 0)) {
    throw std::invalid_argument("Invalid buffered WASAPI graph runner capacity");
  }
  if (capture_stream != nullptr) {
    capture_path_.emplace(input_channels, capture_packet_capacity_frames,
                          fifo_capacity_frames);
  }
  if (render_stream != nullptr) {
    render_path_.emplace(output_channels, render_packet_capacity_frames,
                         fifo_capacity_frames);
    if (prime_render_silence) {
      static_cast<void>(render_path_->fifo.push(output_, graph_block_frames_));
    }
  }
}

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
      auto errors = result.errors();
      if (capture_stream_ != nullptr) {
        auto rollback_result = capture_stream_->stop();
        if (!rollback_result.ok()) {
          errors.insert(errors.end(), rollback_result.errors().begin(),
                        rollback_result.errors().end());
        }
      }
      return WasapiGraphRunnerResult::failure(std::move(errors));
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

void WindowsWasapiGraphRunner::request_stop() noexcept {
  if (render_stream_ != nullptr) {
    render_stream_->request_stop();
  }
  if (capture_stream_ != nullptr) {
    capture_stream_->request_stop();
  }
}

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms) noexcept {
  if (graph_block_frames_ != 0) {
    return process_buffered_once(graph, diagnostics, timeout_ms);
  }

  WasapiGraphRunnerStats stats;

  auto sample_rate_errors =
      validate_graph_sample_rate(graph, capture_stream_, render_stream_);
  if (!sample_rate_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(sample_rate_errors));
  }

  if (capture_stream_ != nullptr) {
    input_.clear();
    auto capture_result = capture_stream_->capture_once(input_, timeout_ms);
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
    auto render_result = render_stream_->render_once(
        output_, static_cast<std::uint32_t>(output_.frames()), timeout_ms);
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

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_buffered_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms) noexcept {
  WasapiGraphRunnerStats stats;
  auto sample_rate_errors =
      validate_graph_sample_rate(graph, capture_stream_, render_stream_);
  if (!sample_rate_errors.empty()) {
    return WasapiGraphRunnerResult::failure(std::move(sample_rate_errors));
  }

  if (capture_stream_ != nullptr) {
    constexpr std::size_t kMaximumCapturePacketsPerCycle = 8;
    const auto packet_limit = render_master_ ? kMaximumCapturePacketsPerCycle : 1;
    for (std::size_t packet_index = 0; packet_index < packet_limit; ++packet_index) {
      capture_path_->packet.clear();
      const auto capture_timeout_ms = render_master_ ? 0 : timeout_ms;
      auto capture_result = capture_stream_->capture_once(
          capture_path_->packet, capture_timeout_ms);
      if (!capture_result.ok()) {
        return WasapiGraphRunnerResult::failure(capture_result.errors());
      }
      if (capture_result.cancelled()) {
        stats.cancelled = true;
        return WasapiGraphRunnerResult::success(stats);
      }
      if (capture_result.frames() == 0) {
        stats.capture_wait_timed_out =
            !render_master_ && capture_result.timed_out();
        break;
      }

      stats.captured_frames += capture_result.frames();
      stats.capture_silent = stats.capture_silent || capture_result.silent();
      stats.capture_data_discontinuity =
          stats.capture_data_discontinuity || capture_result.data_discontinuity();
      stats.capture_timestamp_error =
          stats.capture_timestamp_error || capture_result.timestamp_error();
      if (capture_result.silent()) {
        stats.capture_silent_frames += capture_result.frames();
      }
      if (capture_result.data_discontinuity()) {
        ++diagnostics.xrun_count;
      }

      const auto queued = capture_path_->fifo.push(
          capture_path_->packet, capture_result.frames());
      if (queued < capture_result.frames()) {
        ++diagnostics.capture_fifo_overflow_cycles;
        diagnostics.capture_fifo_overflow_frames +=
            capture_result.frames() - queued;
        ++diagnostics.xrun_count;
      }
    }
    stats.capture_stream_idle = stats.captured_frames == 0;
  }

  diagnostics.capture_fifo_fill_frames =
      capture_path_ ? capture_path_->fifo.available_frames() : 0;
  diagnostics.render_fifo_fill_frames =
      render_path_ ? render_path_->fifo.available_frames() : 0;

  const bool capture_ready =
      !capture_path_ || capture_path_->fifo.available_frames() >= graph_block_frames_;
  const bool render_has_space =
      !render_path_ || render_path_->fifo.free_frames() >= graph_block_frames_;
  if (capture_ready && render_has_space) {
    if (capture_path_) {
      static_cast<void>(capture_path_->fifo.pop(input_, graph_block_frames_));
    }
    auto shape_errors = validate_graph_shape(graph, input_, output_);
    if (!shape_errors.empty()) {
      return WasapiGraphRunnerResult::failure(std::move(shape_errors));
    }
    output_.clear();
    graph.process(input_, output_, diagnostics);
    stats.graph_processed = true;
    if (render_path_) {
      const auto queued = render_path_->fifo.push(output_, graph_block_frames_);
      if (queued != graph_block_frames_) {
        ++diagnostics.render_fifo_overflow_cycles;
        diagnostics.render_fifo_overflow_frames += graph_block_frames_ - queued;
        ++diagnostics.xrun_count;
      }
    }
  }

  if (render_stream_ != nullptr && render_path_->fifo.available_frames() > 0) {
    render_path_->packet.clear();
    const auto staged = render_path_->fifo.peek(
        render_path_->packet, render_path_->packet.frames());
    auto render_result = render_stream_->render_once(
        render_path_->packet, static_cast<std::uint32_t>(staged), timeout_ms);
    if (!render_result.ok()) {
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    if (render_result.cancelled()) {
      diagnostics.capture_fifo_fill_frames =
          capture_path_ ? capture_path_->fifo.available_frames() : 0;
      diagnostics.render_fifo_fill_frames = render_path_->fifo.available_frames();
      stats.cancelled = true;
      return WasapiGraphRunnerResult::success(stats);
    }
    if (render_result.frames() > staged) {
      return WasapiGraphRunnerResult::failure({{
          "render_committed_too_many_frames",
          "WASAPI render stream committed more frames than were staged.",
      }});
    }
    stats.rendered_frames = static_cast<std::uint32_t>(
        render_path_->fifo.consume(render_result.frames()));
    stats.render_stream_idle = stats.rendered_frames == 0;
    stats.render_wait_timed_out = render_result.timed_out();
    stats.render_partial = stats.rendered_frames < staged;
    stats.render_partial_frames =
        stats.render_partial ? static_cast<std::uint32_t>(staged - stats.rendered_frames)
                             : 0;
  } else if (render_stream_ != nullptr) {
    if (!render_master_) {
      stats.render_stream_idle = true;
      ++diagnostics.render_fifo_underflow_cycles;
      diagnostics.render_fifo_underflow_frames += graph_block_frames_;
      diagnostics.capture_fifo_fill_frames =
          capture_path_ ? capture_path_->fifo.available_frames() : 0;
      diagnostics.render_fifo_fill_frames = 0;
      return WasapiGraphRunnerResult::success(stats);
    }
    render_path_->packet.clear();
    auto render_result = render_stream_->render_once(
        render_path_->packet,
        static_cast<std::uint32_t>(render_path_->packet.frames()),
        timeout_ms);
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
    if (stats.rendered_frames > 0) {
      ++diagnostics.render_fifo_underflow_cycles;
      diagnostics.render_fifo_underflow_frames += stats.rendered_frames;
    }
  }

  diagnostics.capture_fifo_fill_frames =
      capture_path_ ? capture_path_->fifo.available_frames() : 0;
  diagnostics.render_fifo_fill_frames =
      render_path_ ? render_path_->fifo.available_frames() : 0;
  return WasapiGraphRunnerResult::success(stats);
}

}  // namespace sar::platform
