#include "core/platform/windows_wasapi_duplex_loop.h"

#include "core/platform/windows_wasapi_loop_preflight.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace sar::platform {

namespace {

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

std::int64_t signed_frame_balance(std::uint64_t captured,
                                  std::uint64_t rendered) noexcept {
  constexpr auto maximum =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (captured >= rendered) {
    const auto difference = captured - rendered;
    return difference > maximum ? std::numeric_limits<std::int64_t>::max()
                                : static_cast<std::int64_t>(difference);
  }

  const auto difference = rendered - captured;
  return difference > maximum ? std::numeric_limits<std::int64_t>::min()
                              : -static_cast<std::int64_t>(difference);
}

}  // namespace

WindowsWasapiDuplexLoop::~WindowsWasapiDuplexLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiDuplexLoop::start(std::uint32_t timeout_ms) {
  capture_clock_baseline_available_ = false;
  render_clock_baseline_available_ = false;
  const auto result = worker_.start(timeout_ms);
  if (result.ok()) {
    capture_clock_baseline_available_ =
        capture_stream_.read_clock(capture_clock_baseline_);
    render_clock_baseline_available_ =
        render_stream_.read_clock(render_clock_baseline_);
  }
  return result;
}

void WindowsWasapiDuplexLoop::stop() noexcept {
  worker_.stop();
}

bool WindowsWasapiDuplexLoop::running() const noexcept {
  return worker_.running();
}

const WasapiStreamProbe& WindowsWasapiDuplexLoop::capture_probe() const noexcept {
  return capture_stream_.probe();
}

const WasapiStreamProbe& WindowsWasapiDuplexLoop::render_probe() const noexcept {
  return render_stream_.probe();
}

WasapiStreamDiagnostics WindowsWasapiDuplexLoop::capture_diagnostics() const noexcept {
  return capture_stream_.diagnostics();
}

WasapiStreamDiagnostics WindowsWasapiDuplexLoop::render_diagnostics() const noexcept {
  return render_stream_.diagnostics();
}

WasapiRealtimeWorkerStats WindowsWasapiDuplexLoop::stats() const noexcept {
  return worker_.stats();
}

WasapiDuplexLoopSummary WindowsWasapiDuplexLoop::summary() const {
  WasapiDuplexLoopSummary result;
  const auto errors = last_errors();
  result.running = running();
  result.error_count = errors.size();
  result.capture_stream = capture_diagnostics();
  result.render_stream = render_diagnostics();
  result.worker = stats();
  result.runtime = summarize_wasapi_runtime(
      result.worker, errors, &result.capture_stream, &result.render_stream);
  result.capture_clock_available = capture_stream_.read_clock(result.capture_clock);
  result.render_clock_available = render_stream_.read_clock(result.render_clock);
  if (capture_clock_baseline_available_ && result.capture_clock_available) {
    const realtime::ClockDomain domain{1, capture_probe().mix_format.sample_rate};
    result.capture_drift = realtime::ClockDriftEstimator::estimate(
        {domain, capture_clock_baseline_.position,
         capture_clock_baseline_.qpc_position_100ns},
        {domain, result.capture_clock.position, result.capture_clock.qpc_position_100ns});
  }
  if (render_clock_baseline_available_ && result.render_clock_available) {
    const realtime::ClockDomain domain{2, render_probe().mix_format.sample_rate};
    result.render_drift = realtime::ClockDriftEstimator::estimate(
        {domain, render_clock_baseline_.position,
         render_clock_baseline_.qpc_position_100ns},
        {domain, result.render_clock.position, result.render_clock.qpc_position_100ns});
  }
  result.frame_balance =
      signed_frame_balance(result.worker.captured_frames, result.worker.rendered_frames);
  return result;
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiDuplexLoop::last_errors() const {
  return worker_.last_errors();
}

WindowsWasapiDuplexLoop::WindowsWasapiDuplexLoop(
    WindowsWasapiStream capture_stream,
    WindowsWasapiStream render_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics)
    : capture_stream_(std::move(capture_stream)),
      render_stream_(std::move(render_stream)),
      runner_(&capture_stream_,
              &render_stream_,
              capture_stream_.probe().mix_format.channels,
              render_stream_.probe().mix_format.channels,
              graph.frames(),
              capture_stream_.probe().buffer_frames,
              render_stream_.probe().buffer_frames,
              graph.frames() + std::max(capture_stream_.probe().buffer_frames,
                                        render_stream_.probe().buffer_frames)),
      worker_(runner_, graph, diagnostics) {}

WasapiDuplexLoopOpenResult WasapiDuplexLoopOpenResult::success(
    std::unique_ptr<WindowsWasapiDuplexLoop> loop) {
  return {std::move(loop), {}};
}

WasapiDuplexLoopOpenResult WasapiDuplexLoopOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiDuplexLoopOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiDuplexLoop& WasapiDuplexLoopOpenResult::loop() noexcept {
  return *loop_;
}

const WindowsWasapiDuplexLoop& WasapiDuplexLoopOpenResult::loop() const noexcept {
  return *loop_;
}

std::unique_ptr<WindowsWasapiDuplexLoop> WasapiDuplexLoopOpenResult::take_loop() noexcept {
  return std::move(loop_);
}

const std::vector<WasapiRealtimeWorkerError>& WasapiDuplexLoopOpenResult::errors()
    const noexcept {
  return errors_;
}

WasapiDuplexLoopOpenResult::WasapiDuplexLoopOpenResult(
    std::unique_ptr<WindowsWasapiDuplexLoop> loop,
    std::vector<WasapiRealtimeWorkerError> errors)
    : loop_(std::move(loop)), errors_(std::move(errors)) {}

WasapiDuplexLoopOpenResult open_default_wasapi_duplex_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics) {
  auto capture_result = open_default_wasapi_stream_shell(WasapiStreamDirection::Capture);
  if (!capture_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(capture_result.errors()));
  }

  auto render_result = open_default_wasapi_stream_shell(WasapiStreamDirection::Render);
  if (!render_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(render_result.errors()));
  }

  if (!compatible_wasapi_duplex_sample_rates(capture_result.stream().probe(),
                                             render_result.stream().probe())) {
    return WasapiDuplexLoopOpenResult::failure({
        {
            "duplex_sample_rate_mismatch",
            "Default WASAPI capture and render streams need a sample-rate adapter before duplex use.",
        },
    });
  }

  auto graph_errors =
      validate_wasapi_duplex_graph_preflight(graph,
                                            capture_result.stream().probe(),
                                            render_result.stream().probe());
  if (!graph_errors.empty()) {
    return WasapiDuplexLoopOpenResult::failure(std::move(graph_errors));
  }

  auto loop = std::unique_ptr<WindowsWasapiDuplexLoop>(
      new WindowsWasapiDuplexLoop(capture_result.take_stream(),
                                  render_result.take_stream(),
                                  graph,
                                  diagnostics));
  return WasapiDuplexLoopOpenResult::success(std::move(loop));
}

}  // namespace sar::platform
