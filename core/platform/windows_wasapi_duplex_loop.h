#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_duplex_supervisor.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream.h"
#include "core/realtime/clock_drift_estimator.h"

#include <cstddef>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace sar::platform {

class WasapiDuplexLoopOpenResult;
struct WindowsWasapiDuplexLoopTestAccess;

[[nodiscard]] bool wasapi_clock_position_to_audio_frames(
    std::uint64_t position,
    std::uint64_t frequency,
    std::uint32_t sample_rate,
    std::uint64_t& audio_frames) noexcept;

[[nodiscard]] bool wasapi_capture_clock_baseline_is_trustworthy(
    std::uint64_t captured_frames,
    const WasapiClockSnapshot& snapshot) noexcept;

enum class WasapiDuplexClockObservationStatus {
  WarmingUp,
  Ready,
  Invalid,
};

struct WasapiDuplexClockObservation {
  WasapiDuplexClockObservationStatus status =
      WasapiDuplexClockObservationStatus::WarmingUp;
  realtime::ClockRateFeedForward feed_forward;
  std::uint64_t window_duration_100ns = 0;
};

class WasapiDuplexClockFeedForwardEstimator {
 public:
  WasapiDuplexClockFeedForwardEstimator(
      std::uint32_t capture_sample_rate,
      std::uint32_t capture_quantum_frames,
      std::uint32_t render_sample_rate,
      std::uint32_t render_quantum_frames) noexcept;

  [[nodiscard]] WasapiDuplexClockObservation observe(
      const WasapiClockSnapshot& capture,
      const WasapiClockSnapshot& render) noexcept;
  [[nodiscard]] std::uint64_t minimum_window_duration_100ns() const noexcept;

 private:
  void set_anchor(const WasapiClockSnapshot& capture,
                  const WasapiClockSnapshot& render) noexcept;

  std::uint32_t capture_sample_rate_ = 0;
  std::uint32_t render_sample_rate_ = 0;
  std::uint64_t minimum_window_duration_100ns_ = 0;
  WasapiClockSnapshot capture_anchor_;
  WasapiClockSnapshot render_anchor_;
  bool anchor_available_ = false;
};

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
  double capture_clock_feed_forward_ppm = 0.0;
  bool capture_clock_available = false;
  bool render_clock_available = false;
  bool capture_clock_feed_forward_valid = false;
};

class WindowsWasapiDuplexLoop : public WasapiDuplexRuntime {
 public:
  WindowsWasapiDuplexLoop(const WindowsWasapiDuplexLoop&) = delete;
  WindowsWasapiDuplexLoop& operator=(const WindowsWasapiDuplexLoop&) = delete;
  ~WindowsWasapiDuplexLoop();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms) override;
  void stop() noexcept override;

  [[nodiscard]] bool running() const noexcept override;
  [[nodiscard]] const WasapiStreamProbe& capture_probe() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& render_probe() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics capture_diagnostics() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics render_diagnostics() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept override;
  [[nodiscard]] WasapiDuplexLoopSummary summary() const;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const override;

 private:
  friend class WasapiDuplexLoopOpenResult;
  friend struct WindowsWasapiDuplexLoopTestAccess;
  friend WasapiDuplexLoopOpenResult open_default_wasapi_duplex_loop(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSource* external_input);
  friend WasapiDuplexLoopOpenResult open_wasapi_duplex_loop(
      const std::string& capture_device_id,
      const std::string& render_device_id,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      RealtimeAudioSource* external_input);

  using ProbeStreamFunction = WasapiStreamProbeResult (*)(
      const std::string* device_id,
      WasapiStreamDirection direction,
      void* context);
  using OpenStreamFunction = WasapiStreamOpenResult (*)(
      WasapiStreamProbe probe,
      std::uint32_t requested_sample_rate,
      void* context);

  static WasapiDuplexLoopOpenResult open(
      const std::string* capture_device_id,
      const std::string* render_device_id,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      ProbeStreamFunction probe_stream,
      OpenStreamFunction open_stream,
      void* context,
      RealtimeAudioSource* external_input);

  WindowsWasapiDuplexLoop(WindowsWasapiStream capture_stream,
                          WindowsWasapiStream render_stream,
                          graph::Graph& graph,
                          diagnostics::EngineDiagnostics& diagnostics,
                          RealtimeAudioSource* external_input);
  void establish_capture_clock_baseline(std::uint32_t timeout_ms) noexcept;
  void run_clock_observer() noexcept;
  void stop_clock_observer() noexcept;

  WindowsWasapiStream capture_stream_;
  WindowsWasapiStream render_stream_;
  diagnostics::EngineDiagnostics& diagnostics_;
  WindowsWasapiGraphRunner runner_;
  WindowsWasapiRealtimeWorker worker_;
  WasapiClockSnapshot capture_clock_baseline_;
  WasapiClockSnapshot render_clock_baseline_;
  bool capture_clock_baseline_available_ = false;
  bool render_clock_baseline_available_ = false;
  std::atomic_bool capture_clock_feed_forward_valid_ = false;
  std::mutex clock_observer_mutex_;
  std::condition_variable clock_observer_condition_;
  bool clock_observer_stop_requested_ = false;
  std::thread clock_observer_thread_;
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
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input = nullptr);

[[nodiscard]] WasapiDuplexLoopOpenResult open_wasapi_duplex_loop(
    const std::string& capture_device_id,
    const std::string& render_device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input = nullptr);

}  // namespace sar::platform
