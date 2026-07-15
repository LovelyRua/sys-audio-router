#include "core/platform/windows_wasapi_runtime_summary.h"

#include <iomanip>
#include <sstream>

namespace sar::platform {

namespace {

const char* bool_token(bool value) noexcept {
  return value ? "1" : "0";
}

std::string format_native_code(std::uint32_t value) {
  std::ostringstream stream;
  stream << "0x" << std::uppercase << std::hex << std::setfill('0')
         << std::setw(8) << value;
  return stream.str();
}

template <typename T, typename Formatter>
std::string format_optional_native_code(const std::optional<T>& value,
                                        Formatter formatter) {
  return value.has_value() ? formatter(*value) : "none";
}

}  // namespace

std::string format_wasapi_native_hresult(std::int32_t value) {
  return format_native_code(static_cast<std::uint32_t>(value));
}

std::string format_wasapi_native_win32_code(std::uint32_t value) {
  return format_native_code(value);
}

const char* wasapi_runtime_health_name(WasapiRuntimeHealth health) noexcept {
  switch (health) {
    case WasapiRuntimeHealth::Stopped:
      return "stopped";
    case WasapiRuntimeHealth::Healthy:
      return "healthy";
    case WasapiRuntimeHealth::Degraded:
      return "degraded";
    case WasapiRuntimeHealth::Faulted:
      return "faulted";
  }
  return "unknown";
}

bool wasapi_runtime_summary_should_fail(const WasapiRuntimeSummary& summary,
                                        bool require_healthy) noexcept {
  if (summary.health == WasapiRuntimeHealth::Faulted) {
    return true;
  }
  return require_healthy && summary.health != WasapiRuntimeHealth::Healthy;
}

std::string format_wasapi_runtime_summary_line(
    const WasapiRuntimeSummary& summary) {
  std::ostringstream stream;
  stream << "wasapi_runtime_summary"
         << " health=" << wasapi_runtime_health_name(summary.health)
         << " reason_code=" << summary.reason_code
         << " loop_cycles=" << summary.loop_cycles
         << " graph_processed_cycles=" << summary.graph_processed_cycles
         << " idle_cycles=" << summary.idle_cycles
         << " wait_timeout_cycles=" << summary.wait_timeout_cycles
         << " capture_wait_timeout_cycles="
         << summary.capture_wait_timeout_cycles
         << " render_wait_timeout_cycles=" << summary.render_wait_timeout_cycles
         << " duplex_event_wait_timeout_cycles="
         << summary.duplex_event_wait_timeout_cycles
         << " capture_partial_cycles=" << summary.capture_partial_cycles
         << " render_partial_cycles=" << summary.render_partial_cycles
         << " capture_partial_frames=" << summary.capture_partial_frames
         << " render_partial_frames=" << summary.render_partial_frames
         << " capture_silent_cycles=" << summary.capture_silent_cycles
         << " capture_silent_frames=" << summary.capture_silent_frames
         << " capture_discontinuity_cycles="
         << summary.capture_discontinuity_cycles
         << " capture_discontinuity_frames="
         << summary.capture_discontinuity_frames
         << " capture_timestamp_error_cycles="
         << summary.capture_timestamp_error_cycles
         << " capture_timestamp_error_frames="
         << summary.capture_timestamp_error_frames
         << " render_startup_silence_cycles="
         << summary.render_startup_silence_cycles
         << " render_startup_silence_frames="
         << summary.render_startup_silence_frames
         << " render_capture_starvation_silence_cycles="
         << summary.render_capture_starvation_silence_cycles
         << " render_capture_starvation_silence_frames="
         << summary.render_capture_starvation_silence_frames
         << " render_recovery_silence_cycles="
         << summary.render_recovery_silence_cycles
         << " render_recovery_silence_frames="
         << summary.render_recovery_silence_frames
         << " render_recovery_silence_episodes="
         << summary.render_recovery_silence_episodes
         << " maximum_render_recovery_silence_frames="
         << summary.maximum_render_recovery_silence_frames
         << " process_error_cycles=" << summary.process_error_cycles
         << " stream_start_error_cycles=" << summary.stream_start_error_cycles
         << " stream_stop_error_cycles=" << summary.stream_stop_error_cycles
         << " stream_wait_cancellation_cycles="
         << summary.stream_wait_cancellation_cycles
         << " xrun_count=" << summary.xrun_count
         << " capture_fifo_fill_frames=" << summary.capture_fifo_fill_frames
         << " render_fifo_fill_frames=" << summary.render_fifo_fill_frames
         << " capture_fifo_overflow_cycles="
         << summary.capture_fifo_overflow_cycles
         << " capture_fifo_overflow_frames="
         << summary.capture_fifo_overflow_frames
         << " render_fifo_overflow_cycles="
         << summary.render_fifo_overflow_cycles
         << " render_fifo_overflow_frames="
         << summary.render_fifo_overflow_frames
         << " render_fifo_underflow_cycles="
         << summary.render_fifo_underflow_cycles
         << " render_fifo_underflow_frames="
         << summary.render_fifo_underflow_frames
         << " last_callback_ns=" << summary.last_callback_nanoseconds
         << " peak_callback_ns=" << summary.peak_callback_nanoseconds
         << " total_callback_ns=" << summary.total_callback_nanoseconds
         << " average_callback_ns=" << summary.average_callback_nanoseconds
         << " captured_frames=" << summary.captured_frames
         << " rendered_frames=" << summary.rendered_frames
         << " last_captured_frames=" << summary.last_captured_frames
         << " last_rendered_frames=" << summary.last_rendered_frames
         << " last_stop_wait_us=" << summary.last_stop_wait_microseconds
         << " has_capture_stream=" << (summary.has_capture_stream ? 1 : 0)
         << " has_render_stream=" << (summary.has_render_stream ? 1 : 0)
         << " capture_sample_rate=" << summary.capture_sample_rate
         << " render_sample_rate=" << summary.render_sample_rate
         << " capture_channels=" << summary.capture_channels
         << " render_channels=" << summary.render_channels
         << " capture_frames_per_block=" << summary.capture_frames_per_block
         << " render_frames_per_block=" << summary.render_frames_per_block
         << " capture_buffer_frames=" << summary.capture_buffer_frames
         << " render_buffer_frames=" << summary.render_buffer_frames
         << " capture_default_period_100ns="
         << summary.capture_default_period_100ns
         << " render_default_period_100ns="
         << summary.render_default_period_100ns
         << " capture_minimum_period_100ns="
         << summary.capture_minimum_period_100ns
         << " render_minimum_period_100ns="
         << summary.render_minimum_period_100ns
         << " last_graph_processed="
         << bool_token(summary.last_graph_processed)
         << " last_capture_idle=" << bool_token(summary.last_capture_idle)
         << " last_render_idle=" << bool_token(summary.last_render_idle)
         << " last_capture_wait_timed_out="
         << bool_token(summary.last_capture_wait_timed_out)
         << " last_render_wait_timed_out="
         << bool_token(summary.last_render_wait_timed_out)
         << " last_capture_partial="
         << bool_token(summary.last_capture_partial)
         << " last_render_partial=" << bool_token(summary.last_render_partial)
         << " last_capture_silent="
         << bool_token(summary.last_capture_silent)
         << " last_capture_discontinuity="
         << bool_token(summary.last_capture_discontinuity)
         << " last_capture_timestamp_error="
         << bool_token(summary.last_capture_timestamp_error)
         << " capture_rate_adapter_recovering="
         << bool_token(summary.capture_rate_adapter_recovering)
         << " last_render_startup_silence="
         << bool_token(summary.last_render_startup_silence)
         << " last_render_capture_starvation_silence="
         << bool_token(summary.last_render_capture_starvation_silence)
         << " last_render_recovery_silence="
         << bool_token(summary.last_render_recovery_silence)
         << " error_count=" << summary.error_count
         << " first_error_code=" << summary.first_error_code
         << " first_error_native_hresult="
         << format_optional_native_code(summary.first_error_native_hresult,
                                        format_wasapi_native_hresult)
         << " first_error_native_win32_code="
         << format_optional_native_code(summary.first_error_native_win32_code,
                                        format_wasapi_native_win32_code);
  return stream.str();
}

WasapiRuntimeSummary summarize_wasapi_runtime(
    const WasapiRealtimeWorkerStats& stats,
    const std::vector<WasapiRealtimeWorkerError>& errors,
    const WasapiStreamDiagnostics* capture_diagnostics,
    const WasapiStreamDiagnostics* render_diagnostics,
    const diagnostics::EngineDiagnostics* engine_diagnostics) {
  WasapiRuntimeSummary summary;
  summary.loop_cycles = stats.loop_cycles;
  summary.graph_processed_cycles = stats.graph_processed_cycles;
  summary.idle_cycles = stats.idle_cycles;
  summary.wait_timeout_cycles = stats.wait_timeout_cycles;
  summary.capture_wait_timeout_cycles = stats.capture_wait_timeout_cycles;
  summary.render_wait_timeout_cycles = stats.render_wait_timeout_cycles;
  summary.duplex_event_wait_timeout_cycles =
      stats.duplex_event_wait_timeout_cycles;
  summary.capture_partial_cycles = stats.capture_partial_cycles;
  summary.render_partial_cycles = stats.render_partial_cycles;
  summary.capture_partial_frames = stats.capture_partial_frames;
  summary.render_partial_frames = stats.render_partial_frames;
  summary.capture_silent_cycles = stats.capture_silent_cycles;
  summary.capture_silent_frames = stats.capture_silent_frames;
  summary.capture_discontinuity_cycles = stats.capture_discontinuity_cycles;
  summary.capture_discontinuity_frames = stats.capture_discontinuity_frames;
  summary.capture_timestamp_error_cycles = stats.capture_timestamp_error_cycles;
  summary.capture_timestamp_error_frames = stats.capture_timestamp_error_frames;
  summary.render_startup_silence_cycles = stats.render_startup_silence_cycles;
  summary.render_startup_silence_frames = stats.render_startup_silence_frames;
  summary.render_capture_starvation_silence_cycles =
      stats.render_capture_starvation_silence_cycles;
  summary.render_capture_starvation_silence_frames =
      stats.render_capture_starvation_silence_frames;
  summary.render_recovery_silence_cycles =
      stats.render_recovery_silence_cycles;
  summary.render_recovery_silence_frames =
      stats.render_recovery_silence_frames;
  summary.render_recovery_silence_episodes =
      stats.render_recovery_silence_episodes;
  summary.maximum_render_recovery_silence_frames =
      stats.maximum_render_recovery_silence_frames;
  summary.process_error_cycles = stats.process_error_cycles;
  summary.stream_start_error_cycles = stats.stream_start_error_cycles;
  summary.stream_stop_error_cycles = stats.stream_stop_error_cycles;
  summary.stream_wait_cancellation_cycles = stats.stream_wait_cancellation_cycles;
  summary.xrun_count = stats.xrun_count;
  summary.last_callback_nanoseconds = stats.last_callback_nanoseconds;
  summary.peak_callback_nanoseconds = stats.peak_callback_nanoseconds;
  summary.total_callback_nanoseconds = stats.total_callback_nanoseconds;
  if (stats.graph_processed_cycles > 0) {
    summary.average_callback_nanoseconds =
        stats.total_callback_nanoseconds / stats.graph_processed_cycles;
  }
  summary.captured_frames = stats.captured_frames;
  summary.rendered_frames = stats.rendered_frames;
  summary.last_captured_frames = stats.last_captured_frames;
  summary.last_rendered_frames = stats.last_rendered_frames;
  summary.last_stop_wait_microseconds = stats.last_stop_wait_microseconds;
  summary.last_graph_processed = stats.last_graph_processed;
  summary.last_capture_idle = stats.last_capture_idle;
  summary.last_render_idle = stats.last_render_idle;
  summary.last_capture_wait_timed_out = stats.last_capture_wait_timed_out;
  summary.last_render_wait_timed_out = stats.last_render_wait_timed_out;
  summary.last_capture_partial = stats.last_capture_partial;
  summary.last_render_partial = stats.last_render_partial;
  summary.last_capture_silent = stats.last_capture_silent;
  summary.last_capture_discontinuity = stats.last_capture_discontinuity;
  summary.last_capture_timestamp_error = stats.last_capture_timestamp_error;
  summary.capture_rate_adapter_recovering =
      stats.capture_rate_adapter_recovering;
  summary.last_render_startup_silence = stats.last_render_startup_silence;
  summary.last_render_capture_starvation_silence =
      stats.last_render_capture_starvation_silence;
  summary.last_render_recovery_silence =
      stats.last_render_recovery_silence;
  summary.error_count = errors.size();

  if (engine_diagnostics != nullptr) {
    summary.capture_fifo_fill_frames =
        engine_diagnostics->capture_fifo_fill_frames;
    summary.render_fifo_fill_frames = engine_diagnostics->render_fifo_fill_frames;
    summary.capture_fifo_overflow_cycles =
        engine_diagnostics->capture_fifo_overflow_cycles;
    summary.capture_fifo_overflow_frames =
        engine_diagnostics->capture_fifo_overflow_frames;
    summary.render_fifo_overflow_cycles =
        engine_diagnostics->render_fifo_overflow_cycles;
    summary.render_fifo_overflow_frames =
        engine_diagnostics->render_fifo_overflow_frames;
    summary.render_fifo_underflow_cycles =
        engine_diagnostics->render_fifo_underflow_cycles;
    summary.render_fifo_underflow_frames =
        engine_diagnostics->render_fifo_underflow_frames;
  }

  if (capture_diagnostics != nullptr) {
    summary.has_capture_stream = true;
    summary.capture_sample_rate = capture_diagnostics->mix_format.sample_rate;
    summary.capture_channels = capture_diagnostics->mix_format.channels;
    summary.capture_frames_per_block =
        capture_diagnostics->mix_format.frames_per_block;
    summary.capture_buffer_frames = capture_diagnostics->buffer_frames;
    summary.capture_default_period_100ns =
        capture_diagnostics->default_period_100ns;
    summary.capture_minimum_period_100ns =
        capture_diagnostics->minimum_period_100ns;
  }
  if (render_diagnostics != nullptr) {
    summary.has_render_stream = true;
    summary.render_sample_rate = render_diagnostics->mix_format.sample_rate;
    summary.render_channels = render_diagnostics->mix_format.channels;
    summary.render_frames_per_block = render_diagnostics->mix_format.frames_per_block;
    summary.render_buffer_frames = render_diagnostics->buffer_frames;
    summary.render_default_period_100ns = render_diagnostics->default_period_100ns;
    summary.render_minimum_period_100ns = render_diagnostics->minimum_period_100ns;
  }

  if (!errors.empty()) {
    summary.health = WasapiRuntimeHealth::Faulted;
    summary.reason_code = errors.front().code;
    summary.reason = errors.front().message;
    summary.first_error_code = errors.front().code;
    summary.first_error_message = errors.front().message;
    summary.first_error_native_hresult = errors.front().native_hresult;
    summary.first_error_native_win32_code = errors.front().native_win32_code;
    return summary;
  }

  if (stats.stream_start_error_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Faulted;
    summary.reason_code = "stream_start_error";
    summary.reason = "A WASAPI stream failed to start.";
    return summary;
  }

  if (stats.process_error_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Faulted;
    summary.reason_code = "process_error";
    summary.reason = "The WASAPI graph runner reported a processing error.";
    return summary;
  }

  if (stats.stream_stop_error_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Faulted;
    summary.reason_code = "stream_stop_error";
    summary.reason = "A WASAPI stream failed to stop cleanly.";
    return summary;
  }

  if (stats.capture_discontinuity_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "capture_discontinuity";
    summary.reason =
        "WASAPI reported one or more discontinuities in the capture stream.";
    return summary;
  }

  if (stats.capture_timestamp_error_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "capture_timestamp_error";
    summary.reason =
        "WASAPI reported one or more invalid capture packet timestamps.";
    return summary;
  }

  if (summary.capture_fifo_overflow_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "capture_fifo_overflow";
    summary.reason = "Captured audio was dropped because the capture FIFO was full.";
    return summary;
  }

  if (summary.render_fifo_overflow_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "render_fifo_overflow";
    summary.reason = "Rendered audio was dropped because the render FIFO was full.";
    return summary;
  }

  if (summary.render_fifo_underflow_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "render_fifo_underflow";
    summary.reason = "The render FIFO did not contain audio when the device requested it.";
    return summary;
  }

  if (stats.xrun_count > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "engine_xrun";
    summary.reason = "One or more realtime graph callbacks exceeded their block budget.";
    return summary;
  }

  if (stats.loop_cycles == 0 && stats.graph_processed_cycles == 0 &&
      stats.captured_frames == 0 && stats.rendered_frames == 0) {
    summary.health = WasapiRuntimeHealth::Stopped;
    summary.reason_code = "no_cycles";
    summary.reason = "The WASAPI realtime worker has not completed a loop cycle.";
    return summary;
  }

  if (stats.wait_timeout_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    if (stats.capture_wait_timeout_cycles > 0 &&
        stats.render_wait_timeout_cycles == 0) {
      summary.reason_code = "capture_wait_timeout";
      summary.reason = "One or more WASAPI capture event waits timed out.";
    } else if (stats.render_wait_timeout_cycles > 0 &&
               stats.capture_wait_timeout_cycles == 0) {
      summary.reason_code = "render_wait_timeout";
      summary.reason = "One or more WASAPI render event waits timed out.";
    } else {
      summary.reason_code = "wait_timeout";
      summary.reason = "One or more WASAPI event waits timed out.";
    }
    return summary;
  }

  summary.health = WasapiRuntimeHealth::Healthy;
  summary.reason_code = "running";
  summary.reason = "The WASAPI realtime worker is processing normally.";
  return summary;
}

}  // namespace sar::platform
