#include "tools/wasapi_measure_report.h"

#include <ostream>

namespace sar::tools {

namespace {

const char* audio_sample_format_name(
    platform::AudioSampleFormat format) noexcept {
  switch (format) {
    case platform::AudioSampleFormat::Unknown:
      return "unknown";
    case platform::AudioSampleFormat::PcmInt:
      return "pcm_int";
    case platform::AudioSampleFormat::IeeeFloat:
      return "ieee_float";
  }
  return "unknown";
}

const char* bool_token(bool value) noexcept {
  return value ? "1" : "0";
}

}  // namespace

void print_wasapi_probe(std::ostream& out,
                        const char* label,
                        const platform::WasapiStreamProbe& probe) {
  out << label << '\n';
  out << "  Device: " << probe.device_label << '\n';
  out << "  Mode: " << platform::wasapi_stream_mode_name(probe.mode) << '\n';
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
  out << platform::format_wasapi_runtime_summary_line(summary) << '\n';
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
  out << "  Xruns: " << summary.xrun_count << '\n';
  out << "  Last callback ns: " << summary.last_callback_nanoseconds << '\n';
  out << "  Peak callback ns: " << summary.peak_callback_nanoseconds << '\n';
  out << "  Total callback ns: " << summary.total_callback_nanoseconds << '\n';
  out << "  Average callback ns: " << summary.average_callback_nanoseconds << '\n';
}

void print_wasapi_stream_diagnostics(
    std::ostream& out,
    const char* label,
    const platform::WasapiStreamDiagnostics& diagnostics) {
  out << "wasapi_stream_diagnostics"
      << " state=" << platform::wasapi_stream_state_name(diagnostics.state)
      << " direction="
      << platform::wasapi_stream_direction_name(diagnostics.direction)
      << " mode=" << platform::wasapi_stream_mode_name(diagnostics.mode)
      << " sample_rate=" << diagnostics.mix_format.sample_rate
      << " channels=" << diagnostics.mix_format.channels
      << " frames_per_block=" << diagnostics.mix_format.frames_per_block
      << " bits_per_sample=" << diagnostics.mix_format.bits_per_sample
      << " valid_bits_per_sample="
      << diagnostics.mix_format.valid_bits_per_sample
      << " sample_format="
      << audio_sample_format_name(diagnostics.mix_format.sample_format)
      << " buffer_frames=" << diagnostics.buffer_frames
      << " default_period_100ns=" << diagnostics.default_period_100ns
      << " minimum_period_100ns=" << diagnostics.minimum_period_100ns
      << '\n';
  out << label << '\n';
  out << "  State: " << platform::wasapi_stream_state_name(diagnostics.state) << '\n';
  out << "  Direction: "
      << platform::wasapi_stream_direction_name(diagnostics.direction) << '\n';
  out << "  Mode: " << platform::wasapi_stream_mode_name(diagnostics.mode) << '\n';
  out << "  Sample rate: " << diagnostics.mix_format.sample_rate << '\n';
  out << "  Channels: " << diagnostics.mix_format.channels << '\n';
  out << "  Frames per block: " << diagnostics.mix_format.frames_per_block << '\n';
  out << "  Bits per sample: " << diagnostics.mix_format.bits_per_sample << '\n';
  out << "  Valid bits per sample: "
      << diagnostics.mix_format.valid_bits_per_sample << '\n';
  out << "  Sample format: "
      << audio_sample_format_name(diagnostics.mix_format.sample_format) << '\n';
  out << "  Buffer frames: " << diagnostics.buffer_frames << '\n';
  out << "  Default period 100ns: " << diagnostics.default_period_100ns << '\n';
  out << "  Minimum period 100ns: " << diagnostics.minimum_period_100ns << '\n';
}

void print_wasapi_worker_stats(
    std::ostream& out,
    const platform::WasapiRealtimeWorkerStats& stats) {
  out << "wasapi_worker_stats"
      << " loop_cycles=" << stats.loop_cycles
      << " graph_processed_cycles=" << stats.graph_processed_cycles
      << " idle_cycles=" << stats.idle_cycles
      << " capture_idle_cycles=" << stats.capture_idle_cycles
      << " render_idle_cycles=" << stats.render_idle_cycles
      << " wait_timeout_cycles=" << stats.wait_timeout_cycles
      << " capture_wait_timeout_cycles=" << stats.capture_wait_timeout_cycles
      << " render_wait_timeout_cycles=" << stats.render_wait_timeout_cycles
      << " capture_partial_cycles=" << stats.capture_partial_cycles
      << " capture_partial_frames=" << stats.capture_partial_frames
      << " render_partial_cycles=" << stats.render_partial_cycles
      << " render_partial_frames=" << stats.render_partial_frames
      << " capture_silent_cycles=" << stats.capture_silent_cycles
      << " capture_silent_frames=" << stats.capture_silent_frames
      << " capture_discontinuity_cycles="
      << stats.capture_discontinuity_cycles
      << " capture_discontinuity_frames="
      << stats.capture_discontinuity_frames
      << " capture_timestamp_error_cycles="
      << stats.capture_timestamp_error_cycles
      << " capture_timestamp_error_frames="
      << stats.capture_timestamp_error_frames
      << " stream_start_error_cycles=" << stats.stream_start_error_cycles
      << " stream_stop_error_cycles=" << stats.stream_stop_error_cycles
      << " stream_wait_cancellation_cycles="
      << stats.stream_wait_cancellation_cycles
      << " process_error_cycles=" << stats.process_error_cycles
      << " xrun_count=" << stats.xrun_count
      << " last_callback_ns=" << stats.last_callback_nanoseconds
      << " peak_callback_ns=" << stats.peak_callback_nanoseconds
      << " total_callback_ns=" << stats.total_callback_nanoseconds
      << " captured_frames=" << stats.captured_frames
      << " rendered_frames=" << stats.rendered_frames
      << " last_captured_frames=" << stats.last_captured_frames
      << " last_rendered_frames=" << stats.last_rendered_frames
      << " last_stop_wait_us=" << stats.last_stop_wait_microseconds
      << " last_graph_processed=" << bool_token(stats.last_graph_processed)
      << " last_capture_idle=" << bool_token(stats.last_capture_idle)
      << " last_render_idle=" << bool_token(stats.last_render_idle)
      << " last_capture_wait_timed_out="
      << bool_token(stats.last_capture_wait_timed_out)
      << " last_render_wait_timed_out="
      << bool_token(stats.last_render_wait_timed_out)
      << " last_capture_partial=" << bool_token(stats.last_capture_partial)
      << " last_render_partial=" << bool_token(stats.last_render_partial)
      << " last_capture_silent=" << bool_token(stats.last_capture_silent)
      << " last_capture_discontinuity="
      << bool_token(stats.last_capture_discontinuity)
      << " last_capture_timestamp_error="
      << bool_token(stats.last_capture_timestamp_error)
      << '\n';
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
  out << "  Capture discontinuity cycles: "
      << stats.capture_discontinuity_cycles << '\n';
  out << "  Capture discontinuity frames: "
      << stats.capture_discontinuity_frames << '\n';
  out << "  Capture timestamp error cycles: "
      << stats.capture_timestamp_error_cycles << '\n';
  out << "  Capture timestamp error frames: "
      << stats.capture_timestamp_error_frames << '\n';
  out << "  Captured frames: " << stats.captured_frames << '\n';
  out << "  Rendered frames: " << stats.rendered_frames << '\n';
  out << "  Last captured frames: " << stats.last_captured_frames << '\n';
  out << "  Last rendered frames: " << stats.last_rendered_frames << '\n';
  out << "  Stream start error cycles: " << stats.stream_start_error_cycles << '\n';
  out << "  Stream stop error cycles: " << stats.stream_stop_error_cycles << '\n';
  out << "  Stream wait cancellation cycles: "
      << stats.stream_wait_cancellation_cycles << '\n';
  out << "  Process error cycles: " << stats.process_error_cycles << '\n';
  out << "  Xruns: " << stats.xrun_count << '\n';
  out << "  Last callback ns: " << stats.last_callback_nanoseconds << '\n';
  out << "  Peak callback ns: " << stats.peak_callback_nanoseconds << '\n';
  out << "  Total callback ns: " << stats.total_callback_nanoseconds << '\n';
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
