#include "core/platform/windows_wasapi_duplex_loop.h"

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

bool compatible_duplex_sample_rates(const WasapiStreamProbe& capture_probe,
                                    const WasapiStreamProbe& render_probe) noexcept {
  return capture_probe.mix_format.sample_rate == render_probe.mix_format.sample_rate;
}

}  // namespace

WindowsWasapiDuplexLoop::~WindowsWasapiDuplexLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiDuplexLoop::start(std::uint32_t timeout_ms) {
  return worker_.start(timeout_ms);
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
              capture_stream_.probe().buffer_frames,
              render_stream_.probe().mix_format.channels,
              render_stream_.probe().buffer_frames),
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

  if (!compatible_duplex_sample_rates(capture_result.stream().probe(),
                                      render_result.stream().probe())) {
    return WasapiDuplexLoopOpenResult::failure({
        {
            "duplex_sample_rate_mismatch",
            "Default WASAPI capture and render streams need a sample-rate adapter before duplex use.",
        },
    });
  }

  auto loop = std::unique_ptr<WindowsWasapiDuplexLoop>(
      new WindowsWasapiDuplexLoop(capture_result.take_stream(),
                                  render_result.take_stream(),
                                  graph,
                                  diagnostics));
  return WasapiDuplexLoopOpenResult::success(std::move(loop));
}

}  // namespace sar::platform
