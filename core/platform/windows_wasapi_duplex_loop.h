#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/realtime/clock_drift_estimator.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sar::platform {

class WasapiDuplexLoopOpenResult;

[[nodiscard]] bool wasapi_clock_position_to_audio_frames(
    std::uint64_t position,
    std::uint64_t frequency,
    std::uint32_t sample_rate,
    std::uint64_t& audio_frames) noexcept;

[[nodiscard]] bool wasapi_capture_clock_baseline_is_trustworthy(
    std::uint64_t captured_frames,
    const WasapiClockSnapshot& snapshot) noexcept;

struct WasapiDuplexLoopSummary {
  bool running = false;
  std::size_t error_count = 0;
  WasapiStreamDiagnostics capture_stream;
  WasapiStreamDiagnostics render_stream;
  WasapiRealtimeWorkerStats worker;
  WasapiRuntimeSummary runtime;
  WasapiClockSnapshot capture_clock;
  WasapiClockSnapshot render_clock;
  realtime::ClockDriftEstimate capture_drift;
  realtime::ClockDriftEstimate render_drift;
  std::int64_t frame_balance = 0;
  bool capture_clock_available = false;
  bool render_clock_available = false;
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
  void establish_capture_clock_baseline(std::uint32_t timeout_ms) noexcept;

  WindowsWasapiStream capture_stream_;
  WindowsWasapiStream render_stream_;
  diagnostics::EngineDiagnostics& diagnostics_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
  WasapiClockSnapshot capture_clock_baseline_;
  WasapiClockSnapshot render_clock_baseline_;
  bool capture_clock_baseline_available_ = false;
  bool render_clock_baseline_available_ = false;
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
