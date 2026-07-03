#include "core/platform/windows_wasapi_graph_runner.h"

#include <utility>

namespace sar::platform {

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
    : capture_stream_(capture_stream),
      render_stream_(render_stream),
      input_(channels, frames),
      output_(channels, frames) {}

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

WasapiGraphRunnerResult WindowsWasapiGraphRunner::process_once(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms) noexcept {
  WasapiGraphRunnerStats stats;

  if (capture_stream_ != nullptr) {
    auto capture_result = capture_stream_->capture_once(input_, timeout_ms);
    if (!capture_result.ok()) {
      return WasapiGraphRunnerResult::failure(capture_result.errors());
    }
    stats.captured_frames = capture_result.frames();
    if (stats.captured_frames == 0) {
      return WasapiGraphRunnerResult::success(stats);
    }
  }

  output_.clear();
  graph.process(input_, output_, diagnostics);
  stats.graph_processed = true;

  if (render_stream_ != nullptr) {
    auto render_result = render_stream_->render_once(output_, timeout_ms);
    if (!render_result.ok()) {
      return WasapiGraphRunnerResult::failure(render_result.errors());
    }
    stats.rendered_frames = render_result.frames();
  }

  return WasapiGraphRunnerResult::success(stats);
}

}  // namespace sar::platform
