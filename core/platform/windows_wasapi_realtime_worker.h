#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/wasapi_realtime_error.h"
#include "core/platform/windows_wasapi_graph_runner.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sar::platform {

struct WasapiRealtimeWorkerError {
  std::string code;
  std::string message;
  std::optional<std::int32_t> native_hresult = std::nullopt;
  std::optional<std::uint32_t> native_win32_code = std::nullopt;
};

struct WasapiRealtimeWorkerStats {
  std::uint64_t loop_cycles = 0;
  std::uint64_t graph_processed_cycles = 0;
  std::uint64_t idle_cycles = 0;
  std::uint64_t capture_idle_cycles = 0;
  std::uint64_t render_idle_cycles = 0;
  std::uint64_t wait_timeout_cycles = 0;
  std::uint64_t capture_wait_timeout_cycles = 0;
  std::uint64_t render_wait_timeout_cycles = 0;
  std::uint64_t capture_partial_cycles = 0;
  std::uint64_t render_partial_cycles = 0;
  std::uint64_t capture_partial_frames = 0;
  std::uint64_t render_partial_frames = 0;
  std::uint64_t capture_silent_cycles = 0;
  std::uint64_t capture_silent_frames = 0;
  std::uint64_t capture_discontinuity_cycles = 0;
  std::uint64_t capture_discontinuity_frames = 0;
  std::uint64_t capture_timestamp_error_cycles = 0;
  std::uint64_t capture_timestamp_error_frames = 0;
  std::uint64_t stream_start_error_cycles = 0;
  std::uint64_t stream_stop_error_cycles = 0;
  std::uint64_t stream_wait_cancellation_cycles = 0;
  std::uint64_t process_error_cycles = 0;
  std::uint64_t xrun_count = 0;
  std::uint64_t last_callback_nanoseconds = 0;
  std::uint64_t peak_callback_nanoseconds = 0;
  std::uint64_t total_callback_nanoseconds = 0;
  std::uint64_t captured_frames = 0;
  std::uint64_t rendered_frames = 0;
  std::uint64_t capture_resampler_input_frames = 0;
  std::uint64_t capture_resampler_output_frames = 0;
  double capture_rate_correction_ppm = 0.0;
  double capture_clock_feed_forward_ppm = 0.0;
  double capture_fifo_correction_ppm = 0.0;
  double capture_resampler_ratio = 1.0;
  bool capture_rate_adapter_active = false;
  bool capture_rate_adapter_recovering = false;
  std::uint64_t capture_rate_adapter_reset_cycles = 0;
  std::uint64_t render_recovery_silence_cycles = 0;
  std::uint64_t render_recovery_silence_frames = 0;
  std::uint64_t maximum_render_recovery_silence_frames = 0;
  double minimum_capture_rate_correction_ppm = 0.0;
  double maximum_capture_rate_correction_ppm = 0.0;
  std::uint32_t last_captured_frames = 0;
  std::uint32_t last_rendered_frames = 0;
  bool last_graph_processed = false;
  bool last_capture_idle = false;
  bool last_render_idle = false;
  bool last_capture_wait_timed_out = false;
  bool last_render_wait_timed_out = false;
  bool last_capture_partial = false;
  bool last_render_partial = false;
  bool last_capture_silent = false;
  bool last_capture_discontinuity = false;
  bool last_capture_timestamp_error = false;
  bool last_render_recovery_silence = false;
  std::uint64_t last_stop_wait_microseconds = 0;
};

class WasapiRealtimeWorkerResult {
 public:
  static WasapiRealtimeWorkerResult success();
  static WasapiRealtimeWorkerResult failure(std::vector<WasapiRealtimeWorkerError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  explicit WasapiRealtimeWorkerResult(std::vector<WasapiRealtimeWorkerError> errors);

  std::vector<WasapiRealtimeWorkerError> errors_;
};

class WindowsWasapiRealtimeWorker {
 public:
  WindowsWasapiRealtimeWorker(WindowsWasapiGraphRunner& runner,
                              graph::Graph& graph,
                              diagnostics::EngineDiagnostics& diagnostics);
  WindowsWasapiRealtimeWorker(const WindowsWasapiRealtimeWorker&) = delete;
  WindowsWasapiRealtimeWorker& operator=(const WindowsWasapiRealtimeWorker&) = delete;
  ~WindowsWasapiRealtimeWorker();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms);
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint64_t processed_cycles() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  void run(std::uint32_t timeout_ms) noexcept;
  void publish_startup_result(bool succeeded) noexcept;
  void set_errors(std::vector<WasapiRealtimeWorkerError> errors);
  void append_errors(std::vector<WasapiRealtimeWorkerError> errors);

  WindowsWasapiGraphRunner& runner_;
  graph::Graph& graph_;
  diagnostics::EngineDiagnostics& diagnostics_;
  std::atomic_bool stop_requested_ = false;
  std::atomic_bool running_ = false;
  std::atomic_uint64_t loop_cycles_ = 0;
  std::atomic_uint64_t graph_processed_cycles_ = 0;
  std::atomic_uint64_t idle_cycles_ = 0;
  std::atomic_uint64_t capture_idle_cycles_ = 0;
  std::atomic_uint64_t render_idle_cycles_ = 0;
  std::atomic_uint64_t wait_timeout_cycles_ = 0;
  std::atomic_uint64_t capture_wait_timeout_cycles_ = 0;
  std::atomic_uint64_t render_wait_timeout_cycles_ = 0;
  std::atomic_uint64_t capture_partial_cycles_ = 0;
  std::atomic_uint64_t render_partial_cycles_ = 0;
  std::atomic_uint64_t capture_partial_frames_ = 0;
  std::atomic_uint64_t render_partial_frames_ = 0;
  std::atomic_uint64_t capture_silent_cycles_ = 0;
  std::atomic_uint64_t capture_silent_frames_ = 0;
  std::atomic_uint64_t capture_discontinuity_cycles_ = 0;
  std::atomic_uint64_t capture_discontinuity_frames_ = 0;
  std::atomic_uint64_t capture_timestamp_error_cycles_ = 0;
  std::atomic_uint64_t capture_timestamp_error_frames_ = 0;
  std::atomic_uint64_t stream_start_error_cycles_ = 0;
  std::atomic_uint64_t stream_stop_error_cycles_ = 0;
  std::atomic_uint64_t stream_wait_cancellation_cycles_ = 0;
  std::atomic_uint64_t process_error_cycles_ = 0;
  std::atomic_uint64_t xrun_count_ = 0;
  std::atomic_uint64_t last_callback_nanoseconds_ = 0;
  std::atomic_uint64_t peak_callback_nanoseconds_ = 0;
  std::atomic_uint64_t total_callback_nanoseconds_ = 0;
  std::atomic_uint64_t captured_frames_ = 0;
  std::atomic_uint64_t rendered_frames_ = 0;
  std::atomic_uint64_t capture_resampler_input_frames_ = 0;
  std::atomic_uint64_t capture_resampler_output_frames_ = 0;
  std::atomic_uint64_t capture_rate_correction_bits_ = 0;
  std::atomic_uint64_t capture_clock_feed_forward_bits_ = 0;
  std::atomic_uint64_t capture_fifo_correction_bits_ = 0;
  std::atomic_uint64_t capture_resampler_ratio_bits_ = 0x3FF0000000000000ULL;
  std::atomic_bool capture_rate_adapter_active_ = false;
  std::atomic_bool capture_rate_adapter_recovering_ = false;
  std::atomic_uint64_t capture_rate_adapter_reset_cycles_ = 0;
  std::atomic_uint64_t render_recovery_silence_cycles_ = 0;
  std::atomic_uint64_t render_recovery_silence_frames_ = 0;
  std::atomic_uint64_t maximum_render_recovery_silence_frames_ = 0;
  std::atomic_uint64_t minimum_capture_rate_correction_bits_ = 0;
  std::atomic_uint64_t maximum_capture_rate_correction_bits_ = 0;
  std::atomic<std::uint32_t> last_captured_frames_ = 0;
  std::atomic<std::uint32_t> last_rendered_frames_ = 0;
  std::atomic_bool last_graph_processed_ = false;
  std::atomic_bool last_capture_idle_ = false;
  std::atomic_bool last_render_idle_ = false;
  std::atomic_bool last_capture_wait_timed_out_ = false;
  std::atomic_bool last_render_wait_timed_out_ = false;
  std::atomic_bool last_capture_partial_ = false;
  std::atomic_bool last_render_partial_ = false;
  std::atomic_bool last_capture_silent_ = false;
  std::atomic_bool last_capture_discontinuity_ = false;
  std::atomic_bool last_capture_timestamp_error_ = false;
  std::atomic_bool last_render_recovery_silence_ = false;
  std::atomic_uint64_t last_stop_wait_microseconds_ = 0;
  std::uint64_t xrun_baseline_ = 0;
  std::mutex lifecycle_mutex_;
  std::mutex startup_mutex_;
  std::condition_variable startup_condition_;
  bool startup_complete_ = false;
  bool startup_succeeded_ = false;
  mutable std::mutex errors_mutex_;
  std::vector<WasapiRealtimeWorkerError> last_errors_;
  WasapiRealtimeErrorChannel realtime_errors_;
  std::thread worker_;
};

}  // namespace sar::platform
