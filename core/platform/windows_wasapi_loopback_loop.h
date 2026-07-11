#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sar::platform {

class WasapiLoopbackLoopOpenResult;

struct WasapiLoopbackLoopSummary {
  bool running = false;
  std::size_t error_count = 0;
  WasapiStreamDiagnostics capture_stream;
  WasapiRealtimeWorkerStats worker;
  WasapiRuntimeSummary runtime;
};

class WindowsWasapiLoopbackLoop {
 public:
  WindowsWasapiLoopbackLoop(const WindowsWasapiLoopbackLoop&) = delete;
  WindowsWasapiLoopbackLoop& operator=(const WindowsWasapiLoopbackLoop&) = delete;
  ~WindowsWasapiLoopbackLoop();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms);
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& capture_probe() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics capture_diagnostics() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept;
  [[nodiscard]] WasapiLoopbackLoopSummary summary() const;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  friend class WasapiLoopbackLoopOpenResult;
  friend WasapiLoopbackLoopOpenResult open_default_wasapi_loopback_loop(
      graph::Graph&, diagnostics::EngineDiagnostics&);

  WindowsWasapiLoopbackLoop(WindowsWasapiStream capture_stream,
                            graph::Graph& graph,
                            diagnostics::EngineDiagnostics& diagnostics);

  WindowsWasapiStream capture_stream_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
};

class WasapiLoopbackLoopOpenResult {
 public:
  static WasapiLoopbackLoopOpenResult success(std::unique_ptr<WindowsWasapiLoopbackLoop> loop);
  static WasapiLoopbackLoopOpenResult failure(std::vector<WasapiRealtimeWorkerError> errors);
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsWasapiLoopbackLoop& loop() noexcept;
  [[nodiscard]] const WindowsWasapiLoopbackLoop& loop() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsWasapiLoopbackLoop> take_loop() noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  WasapiLoopbackLoopOpenResult(std::unique_ptr<WindowsWasapiLoopbackLoop> loop,
                               std::vector<WasapiRealtimeWorkerError> errors);
  std::unique_ptr<WindowsWasapiLoopbackLoop> loop_;
  std::vector<WasapiRealtimeWorkerError> errors_;
};

[[nodiscard]] WasapiLoopbackLoopOpenResult open_default_wasapi_loopback_loop(
    graph::Graph& graph, diagnostics::EngineDiagnostics& diagnostics);

}  // namespace sar::platform
