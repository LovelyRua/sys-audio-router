#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sar::platform {

class WasapiDuplexLoopOpenResult;

struct WasapiDuplexLoopSummary {
  bool running = false;
  std::size_t error_count = 0;
  WasapiStreamDiagnostics capture_stream;
  WasapiStreamDiagnostics render_stream;
  WasapiRealtimeWorkerStats worker;
};

class WindowsWasapiDuplexLoop {
 public:
  WindowsWasapiDuplexLoop(const WindowsWasapiDuplexLoop&) = delete;
  WindowsWasapiDuplexLoop& operator=(const WindowsWasapiDuplexLoop&) = delete;
  ~WindowsWasapiDuplexLoop();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms);
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& capture_probe() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& render_probe() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics capture_diagnostics() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics render_diagnostics() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept;
  [[nodiscard]] WasapiDuplexLoopSummary summary() const;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  friend class WasapiDuplexLoopOpenResult;
  friend WasapiDuplexLoopOpenResult open_default_wasapi_duplex_loop(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics);

  WindowsWasapiDuplexLoop(WindowsWasapiStream capture_stream,
                          WindowsWasapiStream render_stream,
                          graph::Graph& graph,
                          diagnostics::EngineDiagnostics& diagnostics);

  WindowsWasapiStream capture_stream_;
  WindowsWasapiStream render_stream_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
};

class WasapiDuplexLoopOpenResult {
 public:
  static WasapiDuplexLoopOpenResult success(std::unique_ptr<WindowsWasapiDuplexLoop> loop);
  static WasapiDuplexLoopOpenResult failure(std::vector<WasapiRealtimeWorkerError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsWasapiDuplexLoop& loop() noexcept;
  [[nodiscard]] const WindowsWasapiDuplexLoop& loop() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsWasapiDuplexLoop> take_loop() noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  WasapiDuplexLoopOpenResult(std::unique_ptr<WindowsWasapiDuplexLoop> loop,
                             std::vector<WasapiRealtimeWorkerError> errors);

  std::unique_ptr<WindowsWasapiDuplexLoop> loop_;
  std::vector<WasapiRealtimeWorkerError> errors_;
};

[[nodiscard]] WasapiDuplexLoopOpenResult open_default_wasapi_duplex_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics);

}  // namespace sar::platform
