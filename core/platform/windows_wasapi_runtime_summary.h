#pragma once

#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_stream.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

enum class WasapiRuntimeHealth {
  Stopped,
  Healthy,
  Degraded,
  Faulted,
};

struct WasapiRuntimeSummary {
  WasapiRuntimeHealth health = WasapiRuntimeHealth::Stopped;
  std::string reason_code;
  std::string reason;
  std::uint64_t loop_cycles = 0;
  std::uint64_t graph_processed_cycles = 0;
  std::uint64_t idle_cycles = 0;
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
  std::uint64_t process_error_cycles = 0;
  std::uint64_t stream_start_error_cycles = 0;
  std::uint64_t stream_stop_error_cycles = 0;
  std::uint64_t xrun_count = 0;
  std::uint64_t last_callback_nanoseconds = 0;
  std::uint64_t peak_callback_nanoseconds = 0;
  std::uint64_t captured_frames = 0;
  std::uint64_t rendered_frames = 0;
  std::uint32_t last_captured_frames = 0;
  std::uint32_t last_rendered_frames = 0;
  std::uint64_t last_stop_wait_microseconds = 0;
  std::uint32_t capture_sample_rate = 0;
  std::uint32_t render_sample_rate = 0;
  std::uint16_t capture_channels = 0;
  std::uint16_t render_channels = 0;
  std::uint32_t capture_frames_per_block = 0;
  std::uint32_t render_frames_per_block = 0;
  std::uint32_t capture_buffer_frames = 0;
  std::uint32_t render_buffer_frames = 0;
  std::int64_t capture_default_period_100ns = 0;
  std::int64_t render_default_period_100ns = 0;
  std::int64_t capture_minimum_period_100ns = 0;
  std::int64_t render_minimum_period_100ns = 0;
  bool has_capture_stream = false;
  bool has_render_stream = false;
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
  std::size_t error_count = 0;
  std::string first_error_code;
  std::string first_error_message;
};

[[nodiscard]] const char* wasapi_runtime_health_name(
    WasapiRuntimeHealth health) noexcept;

[[nodiscard]] bool wasapi_runtime_summary_should_fail(
    const WasapiRuntimeSummary& summary,
    bool require_healthy) noexcept;

[[nodiscard]] std::string format_wasapi_runtime_summary_line(
    const WasapiRuntimeSummary& summary);

[[nodiscard]] WasapiRuntimeSummary summarize_wasapi_runtime(
    const WasapiRealtimeWorkerStats& stats,
    const std::vector<WasapiRealtimeWorkerError>& errors,
    const WasapiStreamDiagnostics* capture_diagnostics,
    const WasapiStreamDiagnostics* render_diagnostics);

}  // namespace sar::platform
