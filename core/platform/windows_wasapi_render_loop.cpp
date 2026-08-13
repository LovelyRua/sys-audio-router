#include "core/platform/windows_wasapi_render_loop.h"

#include "core/platform/windows_wasapi_loop_preflight.h"

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

WindowsWasapiRenderLoop::~WindowsWasapiRenderLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiRenderLoop::start(std::uint32_t timeout_ms) {
  return worker_.start(timeout_ms);
}

void WindowsWasapiRenderLoop::stop() noexcept {
  worker_.stop();
}

bool WindowsWasapiRenderLoop::running() const noexcept {
  return worker_.running();
}

realtime::AudioBuffer& WindowsWasapiRenderLoop::input_buffer() noexcept {
  return runner_.input_buffer();
}

const realtime::AudioBuffer& WindowsWasapiRenderLoop::input_buffer() const noexcept {
  return runner_.input_buffer();
}

const WasapiStreamProbe& WindowsWasapiRenderLoop::probe() const noexcept {
  return render_stream_.probe();
}

WasapiStreamDiagnostics WindowsWasapiRenderLoop::diagnostics() const noexcept {
  return render_stream_.diagnostics();
}

WasapiRealtimeWorkerStats WindowsWasapiRenderLoop::stats() const noexcept {
  return worker_.stats();
}

WasapiRenderLoopSummary WindowsWasapiRenderLoop::summary() const {
  WasapiRenderLoopSummary result;
  const auto errors = last_errors();
  result.running = running();
  result.error_count = errors.size();
  result.render_stream = diagnostics();
  result.worker = stats();
  result.runtime =
      summarize_wasapi_runtime(result.worker, errors, nullptr, &result.render_stream);
  return result;
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiRenderLoop::last_errors() const {
  return worker_.last_errors();
}

WindowsWasapiRenderLoop::WindowsWasapiRenderLoop(
    WindowsWasapiStream render_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input)
    : render_stream_(std::move(render_stream)),
      runner_(nullptr,
              &render_stream_,
              render_stream_.probe().mix_format.channels,
              render_stream_.probe().mix_format.channels,
              graph.frames(),
              0,
              render_stream_.probe().buffer_frames,
              graph.frames() + render_stream_.probe().buffer_frames,
              false,
              false,
              external_input),
      worker_(runner_, graph, diagnostics) {}

WindowsWasapiRenderLoop::WindowsWasapiRenderLoop(
    WindowsWasapiStream render_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input,
    WasapiGraphChannelLayout channel_layout,
    RealtimeAudioSink* external_output)
    : render_stream_(std::move(render_stream)),
      runner_(nullptr,
              &render_stream_,
              0,
              render_stream_.probe().mix_format.channels,
              graph.frames(),
              0,
              render_stream_.probe().buffer_frames,
              graph.frames() + render_stream_.probe().buffer_frames,
              false,
              false,
              external_input,
              external_output,
              channel_layout),
      worker_(runner_, graph, diagnostics) {}

WasapiRenderLoopOpenResult WindowsWasapiRenderLoop::open_from_stream(
    WasapiStreamOpenResult stream_result,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input,
    WasapiGraphChannelLayout channel_layout,
    RealtimeAudioSink* external_output) {
  if (!stream_result.ok()) {
    return WasapiRenderLoopOpenResult::failure(convert_errors(stream_result.errors()));
  }

  auto graph_errors =
      validate_wasapi_render_graph_preflight(graph, stream_result.stream().probe());
  if (!graph_errors.empty()) {
    return WasapiRenderLoopOpenResult::failure(std::move(graph_errors));
  }

  std::unique_ptr<WindowsWasapiRenderLoop> loop;
  if (channel_layout.graph_input_channels != 0 || external_output != nullptr) {
    loop.reset(new WindowsWasapiRenderLoop(
        stream_result.take_stream(), graph, diagnostics, external_input,
        channel_layout, external_output));
  } else {
    loop.reset(new WindowsWasapiRenderLoop(
        stream_result.take_stream(), graph, diagnostics, external_input));
  }
  return WasapiRenderLoopOpenResult::success(std::move(loop));
}

WasapiRenderLoopOpenResult WasapiRenderLoopOpenResult::success(
    std::unique_ptr<WindowsWasapiRenderLoop> loop) {
  return {std::move(loop), {}};
}

WasapiRenderLoopOpenResult WasapiRenderLoopOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiRenderLoopOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiRenderLoop& WasapiRenderLoopOpenResult::loop() noexcept {
  return *loop_;
}

const WindowsWasapiRenderLoop& WasapiRenderLoopOpenResult::loop() const noexcept {
  return *loop_;
}

std::unique_ptr<WindowsWasapiRenderLoop> WasapiRenderLoopOpenResult::take_loop() noexcept {
  return std::move(loop_);
}

const std::vector<WasapiRealtimeWorkerError>& WasapiRenderLoopOpenResult::errors()
    const noexcept {
  return errors_;
}

WasapiRenderLoopOpenResult::WasapiRenderLoopOpenResult(
    std::unique_ptr<WindowsWasapiRenderLoop> loop,
    std::vector<WasapiRealtimeWorkerError> errors)
    : loop_(std::move(loop)), errors_(std::move(errors)) {}

WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input,
    WasapiGraphChannelLayout channel_layout,
    RealtimeAudioSink* external_output) {
  return WindowsWasapiRenderLoop::open_from_stream(
      open_default_wasapi_stream_shell(
          WasapiStreamDirection::Render, WasapiStreamMode::Endpoint, 0,
          external_input != nullptr ? static_cast<std::uint32_t>(graph.frames()) : 0),
      graph,
      diagnostics,
      external_input,
      channel_layout,
      external_output);
}

WasapiRenderLoopOpenResult open_wasapi_render_loop(
    const std::string& render_device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input,
    WasapiGraphChannelLayout channel_layout,
    RealtimeAudioSink* external_output) {
  if (render_device_id.empty()) {
    return WasapiRenderLoopOpenResult::failure({
        {"missing_render_device_id",
         "Explicit WASAPI render loop requires a render device ID."},
    });
  }
  auto probe = probe_wasapi_stream(render_device_id, WasapiStreamDirection::Render);
  if (!probe.ok()) {
    return WasapiRenderLoopOpenResult::failure(convert_errors(probe.errors()));
  }
  return WindowsWasapiRenderLoop::open_from_stream(
      open_wasapi_stream_shell(
          probe.probe(), 0,
          external_input != nullptr ? static_cast<std::uint32_t>(graph.frames()) : 0),
      graph, diagnostics, external_input, channel_layout, external_output);
}

}  // namespace sar::platform
