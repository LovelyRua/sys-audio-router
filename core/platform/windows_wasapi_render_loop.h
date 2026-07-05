#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sar::platform {

class WasapiRenderLoopOpenResult;

[[nodiscard]] WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics);

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
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  friend class WasapiRenderLoopOpenResult;
  friend WasapiRenderLoopOpenResult open_default_wasapi_render_loop(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics);

  WindowsWasapiRenderLoop(WindowsWasapiStream render_stream,
                          graph::Graph& graph,
                          diagnostics::EngineDiagnostics& diagnostics);

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
    diagnostics::EngineDiagnostics& diagnostics);

}  // namespace sar::platform
