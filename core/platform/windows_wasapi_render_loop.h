#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

class WasapiRenderLoopOpenResult;

struct WasapiRenderLoopSummary {
  bool running = false;
  std::size_t error_count = 0;
  WasapiStreamDiagnostics render_stream;
  WasapiRealtimeWorkerStats worker;
  WasapiRuntimeSummary runtime;
};

[[nodiscard]] WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input = nullptr);
[[nodiscard]] WasapiRenderLoopOpenResult open_wasapi_render_loop(
    const std::string& render_device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input = nullptr);

class WindowsWasapiRenderLoop {
 public:
  WindowsWasapiRenderLoop(const WindowsWasapiRenderLoop&) = delete;
  WindowsWasapiRenderLoop& operator=(const WindowsWasapiRenderLoop&) = delete;
  ~WindowsWasapiRenderLoop();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms);
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] realtime::AudioBuffer& input_buffer() noexcept;
  [[nodiscard]] const realtime::AudioBuffer& input_buffer() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics diagnostics() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept;
  [[nodiscard]] WasapiRenderLoopSummary summary() const;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  friend class WasapiRenderLoopOpenResult;
  friend WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSource* external_input);
  friend WasapiRenderLoopOpenResult open_wasapi_render_loop(
      const std::string& render_device_id,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSource* external_input);

  WindowsWasapiRenderLoop(WindowsWasapiStream render_stream,
                          graph::Graph& graph,
                          diagnostics::EngineDiagnostics& diagnostics,
                          RealtimeAudioSource* external_input);
  [[nodiscard]] static WasapiRenderLoopOpenResult open_from_stream(
      WasapiStreamOpenResult stream_result,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSource* external_input);

  WindowsWasapiStream render_stream_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
};

class WasapiRenderLoopOpenResult {
 public:
  static WasapiRenderLoopOpenResult success(std::unique_ptr<WindowsWasapiRenderLoop> loop);
  static WasapiRenderLoopOpenResult failure(std::vector<WasapiRealtimeWorkerError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsWasapiRenderLoop& loop() noexcept;
  [[nodiscard]] const WindowsWasapiRenderLoop& loop() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsWasapiRenderLoop> take_loop() noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  WasapiRenderLoopOpenResult(std::unique_ptr<WindowsWasapiRenderLoop> loop,
                             std::vector<WasapiRealtimeWorkerError> errors);

  std::unique_ptr<WindowsWasapiRenderLoop> loop_;
  std::vector<WasapiRealtimeWorkerError> errors_;
};

[[nodiscard]] WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input);

}  // namespace sar::platform
