#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_duplex_supervisor.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_stream.h"

#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

class WasapiCaptureLoopOpenResult;

class WindowsWasapiCaptureLoop final : public WasapiDuplexRuntime {
 public:
  WindowsWasapiCaptureLoop(const WindowsWasapiCaptureLoop&) = delete;
  WindowsWasapiCaptureLoop& operator=(const WindowsWasapiCaptureLoop&) = delete;
  ~WindowsWasapiCaptureLoop() override;

  [[nodiscard]] WasapiRealtimeWorkerResult start(
      std::uint32_t timeout_ms) override;
  void stop() noexcept override;
  [[nodiscard]] bool running() const noexcept override;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept override;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors()
      const override;
  [[nodiscard]] WasapiRuntimeSummary runtime_summary() const override;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept;

 private:
  friend class WasapiCaptureLoopOpenResult;
  friend WasapiCaptureLoopOpenResult open_default_wasapi_capture_loop(
      graph::Graph&, diagnostics::EngineDiagnostics&, RealtimeAudioSink*);
  friend WasapiCaptureLoopOpenResult open_wasapi_capture_loop(
      const std::string&, graph::Graph&, diagnostics::EngineDiagnostics&,
      RealtimeAudioSink*);

  WindowsWasapiCaptureLoop(WindowsWasapiStream stream,
                           graph::Graph& graph,
                           diagnostics::EngineDiagnostics& diagnostics,
                           RealtimeAudioSink* output);
  static WasapiCaptureLoopOpenResult open_from_stream(
      WasapiStreamOpenResult stream,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSink* output);

  WindowsWasapiStream stream_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
};

class WasapiCaptureLoopOpenResult {
 public:
  static WasapiCaptureLoopOpenResult success(
      std::unique_ptr<WindowsWasapiCaptureLoop> loop);
  static WasapiCaptureLoopOpenResult failure(
      std::vector<WasapiRealtimeWorkerError> errors);
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsWasapiCaptureLoop> take_loop() noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors()
      const noexcept;

 private:
  WasapiCaptureLoopOpenResult(
      std::unique_ptr<WindowsWasapiCaptureLoop> loop,
      std::vector<WasapiRealtimeWorkerError> errors) noexcept;
  std::unique_ptr<WindowsWasapiCaptureLoop> loop_;
  std::vector<WasapiRealtimeWorkerError> errors_;
};

[[nodiscard]] WasapiCaptureLoopOpenResult open_default_wasapi_capture_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output);
[[nodiscard]] WasapiCaptureLoopOpenResult open_wasapi_capture_loop(
    const std::string& device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSink* output);

}  // namespace sar::platform
