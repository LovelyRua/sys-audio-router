#include "core/platform/windows_wasapi_runtime_summary.h"

namespace sar::platform {

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

WasapiRuntimeSummary summarize_wasapi_runtime(
    const WasapiRealtimeWorkerStats& stats,
    const std::vector<WasapiRealtimeWorkerError>& errors,
    const WasapiStreamDiagnostics* capture_diagnostics,
    const WasapiStreamDiagnostics* render_diagnostics) {
  WasapiRuntimeSummary summary;
  summary.loop_cycles = stats.loop_cycles;
  summary.graph_processed_cycles = stats.graph_processed_cycles;
  summary.idle_cycles = stats.idle_cycles;
  summary.wait_timeout_cycles = stats.wait_timeout_cycles;
  summary.capture_wait_timeout_cycles = stats.capture_wait_timeout_cycles;
  summary.render_wait_timeout_cycles = stats.render_wait_timeout_cycles;
  summary.capture_partial_cycles = stats.capture_partial_cycles;
  summary.render_partial_cycles = stats.render_partial_cycles;
  summary.capture_partial_frames = stats.capture_partial_frames;
  summary.render_partial_frames = stats.render_partial_frames;
  summary.capture_silent_cycles = stats.capture_silent_cycles;
  summary.capture_silent_frames = stats.capture_silent_frames;
  summary.process_error_cycles = stats.process_error_cycles;
  summary.stream_start_error_cycles = stats.stream_start_error_cycles;
  summary.stream_stop_error_cycles = stats.stream_stop_error_cycles;
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
  summary.error_count = errors.size();

  if (capture_diagnostics != nullptr) {
    summary.has_capture_stream = true;
    summary.capture_buffer_frames = capture_diagnostics->buffer_frames;
  }
  if (render_diagnostics != nullptr) {
    summary.has_render_stream = true;
    summary.render_buffer_frames = render_diagnostics->buffer_frames;
  }

  if (!errors.empty()) {
    summary.health = WasapiRuntimeHealth::Faulted;
    summary.reason_code = errors.front().code;
    summary.reason = errors.front().message;
    summary.first_error_code = errors.front().code;
    summary.first_error_message = errors.front().message;
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

  if (stats.capture_partial_cycles > 0 || stats.render_partial_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    if (stats.capture_partial_cycles > 0 && stats.render_partial_cycles == 0) {
      summary.reason_code = "capture_partial_buffer";
      summary.reason =
          "One or more WASAPI capture cycles transferred fewer frames than requested.";
    } else if (stats.render_partial_cycles > 0 &&
               stats.capture_partial_cycles == 0) {
      summary.reason_code = "render_partial_buffer";
      summary.reason =
          "One or more WASAPI render cycles transferred fewer frames than requested.";
    } else {
      summary.reason_code = "partial_buffer";
      summary.reason =
          "One or more WASAPI cycles transferred fewer frames than requested.";
    }
    return summary;
  }

  if (stats.capture_silent_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "silent_capture";
    summary.reason = "One or more capture cycles returned silent audio.";
    return summary;
  }

  if (stats.idle_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "idle_cycle";
    summary.reason = "One or more worker cycles completed without full graph processing.";
    return summary;
  }

  summary.health = WasapiRuntimeHealth::Healthy;
  summary.reason_code = "running";
  summary.reason = "The WASAPI realtime worker is processing normally.";
  return summary;
}

}  // namespace sar::platform
