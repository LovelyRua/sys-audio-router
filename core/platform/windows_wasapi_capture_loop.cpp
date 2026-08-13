#include "core/platform/windows_wasapi_capture_loop.h"

#include <utility>

namespace sar::platform {

namespace {

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message, error.native_hresult,
                         error.native_win32_code});
  }
  return converted;
}

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamProbeError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

}  // namespace

WindowsWasapiCaptureLoop::~WindowsWasapiCaptureLoop() { stop(); }

WasapiRealtimeWorkerResult WindowsWasapiCaptureLoop::start(
    std::uint32_t timeout_ms) {
  return worker_.start(timeout_ms);
}

void WindowsWasapiCaptureLoop::stop() noexcept { worker_.stop(); }

bool WindowsWasapiCaptureLoop::running() const noexcept {
  return worker_.running();
}

WasapiRealtimeWorkerStats WindowsWasapiCaptureLoop::stats() const noexcept {
  return worker_.stats();
}

std::vector<WasapiRealtimeWorkerError>
WindowsWasapiCaptureLoop::last_errors() const {
  return worker_.last_errors();
}

WasapiRuntimeSummary WindowsWasapiCaptureLoop::runtime_summary() const {
  const auto errors = last_errors();
  const auto stream_diagnostics = stream_.diagnostics();
  return summarize_wasapi_runtime(stats(), errors, &stream_diagnostics,
                                  nullptr);
}

const WasapiStreamProbe& WindowsWasapiCaptureLoop::probe() const noexcept {
  return stream_.probe();
}

WindowsWasapiCaptureLoop::WindowsWasapiCaptureLoop(
    WindowsWasapiStream stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output)
    : stream_(std::move(stream)),
      runner_(&stream_, nullptr, stream_.probe().mix_format.channels, 0,
              graph.frames(), stream_.probe().buffer_frames, 0,
              graph.frames() + stream_.probe().buffer_frames, false, false,
              nullptr, output,
              {
                  .graph_input_channels = graph.channels(),
                  .graph_output_channels = graph.channels(),
                  .capture_input_offset = 0,
                  .external_output_offset = 0,
                  .external_output_channels = graph.channels(),
              }),
      worker_(runner_, graph, diagnostics) {}

WasapiCaptureLoopOpenResult WindowsWasapiCaptureLoop::open_from_stream(
    WasapiStreamOpenResult stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output) {
  if (!stream.ok()) {
    return WasapiCaptureLoopOpenResult::failure(
        convert_errors(stream.errors()));
  }
  const auto& probe = stream.stream().probe();
  if (graph.channels() != probe.mix_format.channels ||
      graph.sample_rate() != probe.mix_format.sample_rate) {
    return WasapiCaptureLoopOpenResult::failure({{
        "capture_follower_graph_format_mismatch",
        "Capture follower graph must use the native capture format.",
    }});
  }
  return WasapiCaptureLoopOpenResult::success(
      std::unique_ptr<WindowsWasapiCaptureLoop>(new WindowsWasapiCaptureLoop(
          stream.take_stream(), graph, diagnostics, output)));
}

WasapiCaptureLoopOpenResult WasapiCaptureLoopOpenResult::success(
    std::unique_ptr<WindowsWasapiCaptureLoop> loop) {
  return {std::move(loop), {}};
}

WasapiCaptureLoopOpenResult WasapiCaptureLoopOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiCaptureLoopOpenResult::ok() const noexcept {
  return loop_ != nullptr && errors_.empty();
}

std::unique_ptr<WindowsWasapiCaptureLoop>
WasapiCaptureLoopOpenResult::take_loop() noexcept {
  return std::move(loop_);
}

const std::vector<WasapiRealtimeWorkerError>&
WasapiCaptureLoopOpenResult::errors() const noexcept {
  return errors_;
}

WasapiCaptureLoopOpenResult::WasapiCaptureLoopOpenResult(
    std::unique_ptr<WindowsWasapiCaptureLoop> loop,
    std::vector<WasapiRealtimeWorkerError> errors) noexcept
    : loop_(std::move(loop)), errors_(std::move(errors)) {}

WasapiCaptureLoopOpenResult open_default_wasapi_capture_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output) {
  return WindowsWasapiCaptureLoop::open_from_stream(
      open_default_wasapi_stream_shell(WasapiStreamDirection::Capture), graph,
      diagnostics, output);
}

WasapiCaptureLoopOpenResult open_wasapi_capture_loop(
    const std::string& device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output) {
  if (device_id.empty()) {
    return WasapiCaptureLoopOpenResult::failure({{
        "missing_capture_device_id",
        "Explicit WASAPI capture loop requires a device ID.",
    }});
  }
  auto probe = probe_wasapi_stream(device_id, WasapiStreamDirection::Capture);
  if (!probe.ok()) {
    return WasapiCaptureLoopOpenResult::failure(convert_errors(probe.errors()));
  }
  return WindowsWasapiCaptureLoop::open_from_stream(
      open_wasapi_stream_shell(probe.probe()), graph, diagnostics, output);
}

}  // namespace sar::platform
