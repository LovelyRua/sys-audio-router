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
  summary.process_error_cycles = stats.process_error_cycles;
  summary.stream_start_error_cycles = stats.stream_start_error_cycles;
  summary.captured_frames = stats.captured_frames;
  summary.rendered_frames = stats.rendered_frames;
  summary.last_graph_processed = stats.last_graph_processed;
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

  if (stats.loop_cycles == 0 && stats.graph_processed_cycles == 0 &&
      stats.captured_frames == 0 && stats.rendered_frames == 0) {
    summary.health = WasapiRuntimeHealth::Stopped;
    summary.reason_code = "no_cycles";
    summary.reason = "The WASAPI realtime worker has not completed a loop cycle.";
    return summary;
  }

  if (stats.wait_timeout_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "wait_timeout";
    summary.reason = "One or more WASAPI event waits timed out.";
    return summary;
  }

  if (stats.capture_partial_cycles > 0 || stats.render_partial_cycles > 0) {
    summary.health = WasapiRuntimeHealth::Degraded;
    summary.reason_code = "partial_buffer";
    summary.reason = "One or more WASAPI cycles transferred fewer frames than requested.";
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
