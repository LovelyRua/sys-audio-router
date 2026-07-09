#include "tools/wasapi_measure_report.h"

#include <ostream>

namespace sar::tools {

void print_wasapi_probe(std::ostream& out,
                        const char* label,
                        const platform::WasapiStreamProbe& probe) {
  out << label << '\n';
  out << "  Device: " << probe.device_label << '\n';
  out << "  Sample rate: " << probe.mix_format.sample_rate << '\n';
  out << "  Channels: " << probe.mix_format.channels << '\n';
  out << "  Frames per block: " << probe.mix_format.frames_per_block << '\n';
  out << "  Buffer frames: " << probe.buffer_frames << '\n';
  out << "  Default period 100ns: " << probe.default_period_100ns << '\n';
  out << "  Minimum period 100ns: " << probe.minimum_period_100ns << '\n';
}

void print_wasapi_runtime_summary(
    std::ostream& out,
    const platform::WasapiRuntimeSummary& summary) {
  out << "Runtime summary\n";
  out << "  Health: " << platform::wasapi_runtime_health_name(summary.health) << '\n';
  out << "  Reason code: " << summary.reason_code << '\n';
  out << "  Reason: " << summary.reason << '\n';
  out << "  Error count: " << summary.error_count << '\n';
  if (!summary.first_error_code.empty()) {
    out << "  First error code: " << summary.first_error_code << '\n';
    out << "  First error message: " << summary.first_error_message << '\n';
  }
  out << "  Has capture stream: " << (summary.has_capture_stream ? "yes" : "no") << '\n';
  out << "  Has render stream: " << (summary.has_render_stream ? "yes" : "no") << '\n';
  out << "  Capture sample rate: " << summary.capture_sample_rate << '\n';
  out << "  Render sample rate: " << summary.render_sample_rate << '\n';
  out << "  Capture channels: " << summary.capture_channels << '\n';
  out << "  Render channels: " << summary.render_channels << '\n';
  out << "  Capture frames per block: " << summary.capture_frames_per_block << '\n';
  out << "  Render frames per block: " << summary.render_frames_per_block << '\n';
  out << "  Capture buffer frames: " << summary.capture_buffer_frames << '\n';
  out << "  Render buffer frames: " << summary.render_buffer_frames << '\n';
  out << "  Capture default period 100ns: "
      << summary.capture_default_period_100ns << '\n';
  out << "  Render default period 100ns: "
      << summary.render_default_period_100ns << '\n';
  out << "  Capture minimum period 100ns: "
      << summary.capture_minimum_period_100ns << '\n';
  out << "  Render minimum period 100ns: "
      << summary.render_minimum_period_100ns << '\n';
  out << "  Captured frames: " << summary.captured_frames << '\n';
  out << "  Rendered frames: " << summary.rendered_frames << '\n';
  out << "  Last captured frames: " << summary.last_captured_frames << '\n';
  out << "  Last rendered frames: " << summary.last_rendered_frames << '\n';
  out << "  Last stop wait us: " << summary.last_stop_wait_microseconds << '\n';
}

void print_wasapi_worker_stats(
    std::ostream& out,
    const platform::WasapiRealtimeWorkerStats& stats) {
  out << "Worker stats\n";
  out << "  Loop cycles: " << stats.loop_cycles << '\n';
  out << "  Graph processed cycles: " << stats.graph_processed_cycles << '\n';
  out << "  Idle cycles: " << stats.idle_cycles << '\n';
  out << "  Capture idle cycles: " << stats.capture_idle_cycles << '\n';
  out << "  Render idle cycles: " << stats.render_idle_cycles << '\n';
  out << "  Wait timeout cycles: " << stats.wait_timeout_cycles << '\n';
  out << "  Capture wait timeout cycles: "
      << stats.capture_wait_timeout_cycles << '\n';
  out << "  Render wait timeout cycles: "
      << stats.render_wait_timeout_cycles << '\n';
  out << "  Capture partial cycles: " << stats.capture_partial_cycles << '\n';
  out << "  Capture partial frames: " << stats.capture_partial_frames << '\n';
  out << "  Render partial cycles: " << stats.render_partial_cycles << '\n';
  out << "  Render partial frames: " << stats.render_partial_frames << '\n';
  out << "  Capture silent cycles: " << stats.capture_silent_cycles << '\n';
  out << "  Capture silent frames: " << stats.capture_silent_frames << '\n';
  out << "  Captured frames: " << stats.captured_frames << '\n';
  out << "  Rendered frames: " << stats.rendered_frames << '\n';
  out << "  Last captured frames: " << stats.last_captured_frames << '\n';
  out << "  Last rendered frames: " << stats.last_rendered_frames << '\n';
  out << "  Stream start error cycles: " << stats.stream_start_error_cycles << '\n';
  out << "  Stream stop error cycles: " << stats.stream_stop_error_cycles << '\n';
  out << "  Process error cycles: " << stats.process_error_cycles << '\n';
  out << "  Last stop wait us: " << stats.last_stop_wait_microseconds << '\n';
}

void print_wasapi_engine_diagnostics(
    std::ostream& out,
    const diagnostics::EngineDiagnostics& diagnostics) {
  out << "Engine diagnostics\n";
  out << "  Graph version: " << diagnostics.graph_version << '\n';
  out << "  Processed blocks: " << diagnostics.processed_blocks << '\n';
  out << "  Xruns: " << diagnostics.xrun_count << '\n';
  out << "  Last callback seconds: " << diagnostics.last_callback_seconds << '\n';
  out << "  Peak callback seconds: " << diagnostics.peak_callback_seconds << '\n';
}

}  // namespace sar::tools
