#include "core/platform/windows_wasapi_loopback_loop.h"

#include "core/platform/windows_wasapi_loop_preflight.h"

#include <utility>

namespace sar::platform {

namespace {

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamError>& errors) {
  std::vector<WasapiRealtimeWorkerError> result;
  result.reserve(errors.size());
  for (const auto& error : errors) {
    result.push_back({error.code, error.message, error.native_hresult,
                      error.native_win32_code});
  }
  return result;
}

}  // namespace

WindowsWasapiLoopbackLoop::~WindowsWasapiLoopbackLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiLoopbackLoop::start(
    std::uint32_t timeout_ms) {
  return worker_.start(timeout_ms);
}

void WindowsWasapiLoopbackLoop::stop() noexcept {
  worker_.stop();
}

bool WindowsWasapiLoopbackLoop::running() const noexcept {
  return worker_.running();
}

const WasapiStreamProbe& WindowsWasapiLoopbackLoop::capture_probe() const noexcept {
  return capture_stream_.probe();
}

WasapiStreamDiagnostics WindowsWasapiLoopbackLoop::capture_diagnostics()
    const noexcept {
  return capture_stream_.diagnostics();
}

bool WindowsWasapiLoopbackLoop::read_clock(
    WasapiClockSnapshot& snapshot) noexcept {
  return capture_stream_.read_clock(snapshot);
}

WasapiRealtimeWorkerStats WindowsWasapiLoopbackLoop::stats() const noexcept {
  return worker_.stats();
}

WasapiLoopbackLoopSummary WindowsWasapiLoopbackLoop::summary() const {
  WasapiLoopbackLoopSummary result;
  const auto errors = last_errors();
  result.running = running();
  result.error_count = errors.size();
  result.capture_stream = capture_diagnostics();
  result.worker = stats();
  result.runtime = summarize_wasapi_runtime(
      result.worker, errors, &result.capture_stream, nullptr);
  return result;
}

std::vector<WasapiRealtimeWorkerError>
WindowsWasapiLoopbackLoop::last_errors() const {
  return worker_.last_errors();
}

WindowsWasapiLoopbackLoop::WindowsWasapiLoopbackLoop(
    WindowsWasapiStream capture_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics)
    : capture_stream_(std::move(capture_stream)),
      runner_(&capture_stream_,
              nullptr,
              capture_stream_.probe().mix_format.channels,
              capture_stream_.probe().mix_format.channels,
              graph.frames(),
              capture_stream_.probe().buffer_frames,
              0,
              graph.frames() + capture_stream_.probe().buffer_frames),
      worker_(runner_, graph, diagnostics) {}

WasapiLoopbackLoopOpenResult WasapiLoopbackLoopOpenResult::success(
    std::unique_ptr<WindowsWasapiLoopbackLoop> loop) {
  return {std::move(loop), {}};
}

WasapiLoopbackLoopOpenResult WasapiLoopbackLoopOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiLoopbackLoopOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiLoopbackLoop& WasapiLoopbackLoopOpenResult::loop() noexcept {
  return *loop_;
}

const WindowsWasapiLoopbackLoop& WasapiLoopbackLoopOpenResult::loop()
    const noexcept {
  return *loop_;
}

std::unique_ptr<WindowsWasapiLoopbackLoop>
WasapiLoopbackLoopOpenResult::take_loop() noexcept {
  return std::move(loop_);
}

const std::vector<WasapiRealtimeWorkerError>&
WasapiLoopbackLoopOpenResult::errors() const noexcept {
  return errors_;
}

WasapiLoopbackLoopOpenResult::WasapiLoopbackLoopOpenResult(
    std::unique_ptr<WindowsWasapiLoopbackLoop> loop,
    std::vector<WasapiRealtimeWorkerError> errors)
    : loop_(std::move(loop)), errors_(std::move(errors)) {}

WasapiLoopbackLoopOpenResult open_default_wasapi_loopback_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics) {
  auto stream_result = open_default_wasapi_stream_shell(
      WasapiStreamDirection::Capture, WasapiStreamMode::Loopback);
  if (!stream_result.ok()) {
    return WasapiLoopbackLoopOpenResult::failure(
        convert_errors(stream_result.errors()));
  }

  const auto& probe = stream_result.stream().probe();
  auto graph_errors =
      validate_wasapi_duplex_graph_preflight(graph, probe, probe);
  if (!graph_errors.empty()) {
    return WasapiLoopbackLoopOpenResult::failure(std::move(graph_errors));
  }

  auto loop = std::unique_ptr<WindowsWasapiLoopbackLoop>(
      new WindowsWasapiLoopbackLoop(
          stream_result.take_stream(), graph, diagnostics));
  return WasapiLoopbackLoopOpenResult::success(std::move(loop));
}

}  // namespace sar::platform
