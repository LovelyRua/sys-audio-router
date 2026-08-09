#include "tools/wasapi_measure_report.h"

#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

sar::platform::WasapiStreamProbe make_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = sar::platform::WasapiStreamDirection::Render;
  probe.device_id = "render-device";
  probe.device_label = "Render Device";
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.mix_format.frames_per_block = 128;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  probe.buffer_frames = 256;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  return probe;
}

sar::platform::WasapiRuntimeSummary make_summary() {
  sar::platform::WasapiRuntimeSummary summary;
  summary.health = sar::platform::WasapiRuntimeHealth::Degraded;
  summary.reason_code = "render_wait_timeout";
  summary.reason = "One or more WASAPI render event waits timed out.";
  summary.error_count = 2;
  summary.first_error_code = "first_error";
  summary.first_error_message = "First synthetic error.";
  summary.first_error_native_hresult =
      static_cast<std::int32_t>(0x80004005u);
  summary.first_error_native_win32_code = 5u;
  summary.has_capture_stream = true;
  summary.has_render_stream = true;
  summary.capture_sample_rate = 48000;
  summary.render_sample_rate = 48000;
  summary.capture_channels = 4;
  summary.render_channels = 2;
  summary.capture_frames_per_block = 96;
  summary.render_frames_per_block = 128;
  summary.capture_buffer_frames = 192;
  summary.render_buffer_frames = 256;
  summary.capture_default_period_100ns = 110000;
  summary.render_default_period_100ns = 100000;
  summary.capture_minimum_period_100ns = 40000;
  summary.render_minimum_period_100ns = 30000;
  summary.captured_frames = 960;
  summary.rendered_frames = 1280;
  summary.last_captured_frames = 96;
  summary.last_rendered_frames = 128;
  summary.last_stop_wait_microseconds = 1500;
  summary.capture_fifo_fill_frames = 144;
  summary.render_fifo_fill_frames = 96;
  summary.capture_fifo_overflow_cycles = 2;
  summary.capture_fifo_overflow_frames = 48;
  summary.render_fifo_overflow_cycles = 3;
  summary.render_fifo_overflow_frames = 72;
  summary.render_fifo_underflow_cycles = 4;
  summary.render_fifo_underflow_frames = 24;
  summary.render_recovery_silence_episodes = 6;
  return summary;
}

sar::platform::WasapiStreamDiagnostics make_stream_diagnostics() {
  sar::platform::WasapiStreamDiagnostics diagnostics;
  diagnostics.state = sar::platform::WasapiStreamState::Started;
  diagnostics.direction = sar::platform::WasapiStreamDirection::Capture;
  diagnostics.mode = sar::platform::WasapiStreamMode::Loopback;
  diagnostics.mix_format.sample_rate = 44100;
  diagnostics.mix_format.channels = 1;
  diagnostics.mix_format.frames_per_block = 64;
  diagnostics.mix_format.bits_per_sample = 32;
  diagnostics.mix_format.valid_bits_per_sample = 24;
  diagnostics.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  diagnostics.buffer_frames = 192;
  diagnostics.default_period_100ns = 120000;
  diagnostics.minimum_period_100ns = 40000;
  return diagnostics;
}

sar::platform::WasapiRealtimeWorkerStats make_stats() {
  sar::platform::WasapiRealtimeWorkerStats stats;
  stats.loop_cycles = 10;
  stats.graph_processed_cycles = 9;
  stats.idle_cycles = 1;
  stats.capture_idle_cycles = 2;
  stats.render_idle_cycles = 3;
  stats.wait_timeout_cycles = 4;
  stats.capture_wait_timeout_cycles = 5;
  stats.render_wait_timeout_cycles = 6;
  stats.duplex_event_wait_timeout_cycles = 7;
  stats.capture_partial_cycles = 7;
  stats.capture_partial_frames = 8;
  stats.render_partial_cycles = 9;
  stats.render_partial_frames = 10;
  stats.capture_silent_cycles = 11;
  stats.capture_silent_frames = 12;
  stats.capture_discontinuity_cycles = 13;
  stats.capture_discontinuity_frames = 14;
  stats.capture_timestamp_error_cycles = 15;
  stats.capture_timestamp_error_frames = 16;
  stats.captured_frames = 960;
  stats.rendered_frames = 1280;
  stats.capture_resampler_input_frames = 950;
  stats.capture_resampler_output_frames = 940;
  stats.capture_rate_correction_ppm = 12.5;
  stats.capture_clock_feed_forward_ppm = 4.25;
  stats.capture_fifo_correction_ppm = 8.25;
  stats.capture_resampler_ratio = 0.9999875;
  stats.capture_rate_adapter_active = true;
  stats.capture_rate_correction_clamped = true;
  stats.capture_rate_adapter_recovering = true;
  stats.capture_rate_adapter_reset_cycles = 4;
  stats.capture_rate_clamped_cycles = 21;
  stats.current_consecutive_capture_rate_clamped_cycles = 6;
  stats.maximum_consecutive_capture_rate_clamped_cycles = 9;
  stats.current_consecutive_capture_rate_clamped_frames = 768;
  stats.maximum_consecutive_capture_rate_clamped_frames = 1152;
  stats.render_startup_silence_cycles = 2;
  stats.render_startup_silence_frames = 256;
  stats.render_capture_starvation_silence_cycles = 3;
  stats.render_capture_starvation_silence_frames = 384;
  stats.render_recovery_silence_cycles = 5;
  stats.render_recovery_silence_frames = 640;
  stats.render_recovery_silence_episodes = 4;
  stats.maximum_render_recovery_silence_frames = 192;
  stats.minimum_capture_rate_correction_ppm = -22.0;
  stats.maximum_capture_rate_correction_ppm = 17.0;
  stats.last_captured_frames = 96;
  stats.last_rendered_frames = 128;
  stats.stream_start_error_cycles = 13;
  stats.stream_stop_error_cycles = 14;
  stats.stream_wait_cancellation_cycles = 15;
  stats.process_error_cycles = 15;
  stats.xrun_count = 16;
  stats.last_callback_nanoseconds = 17000;
  stats.peak_callback_nanoseconds = 18000;
  stats.total_callback_nanoseconds = 153000;
  stats.last_stop_wait_microseconds = 1500;
  stats.last_graph_processed = true;
  stats.last_capture_idle = false;
  stats.last_render_idle = true;
  stats.last_capture_wait_timed_out = false;
  stats.last_render_wait_timed_out = true;
  stats.last_capture_partial = true;
  stats.last_render_partial = false;
  stats.last_capture_silent = true;
  stats.last_capture_discontinuity = true;
  stats.last_capture_timestamp_error = false;
  stats.last_render_startup_silence = true;
  stats.last_render_capture_starvation_silence = true;
  stats.last_render_recovery_silence = true;
  return stats;
}

sar::diagnostics::EngineDiagnostics make_diagnostics() {
  sar::diagnostics::EngineDiagnostics diagnostics;
  diagnostics.graph_version = 42;
  diagnostics.processed_blocks = 99;
  diagnostics.xrun_count = 3;
  diagnostics.capture_fifo_fill_frames = 144;
  diagnostics.render_fifo_fill_frames = 96;
  diagnostics.capture_fifo_overflow_cycles = 2;
  diagnostics.capture_fifo_overflow_frames = 48;
  diagnostics.render_fifo_overflow_cycles = 3;
  diagnostics.render_fifo_overflow_frames = 72;
  diagnostics.render_fifo_underflow_cycles = 4;
  diagnostics.render_fifo_underflow_frames = 24;
  diagnostics.last_callback_seconds = 0.001;
  diagnostics.peak_callback_seconds = 0.002;
  return diagnostics;
}

}  // namespace

int main() {
  {
    std::ostringstream out;
    sar::tools::print_wasapi_probe(out, "Default render stream", make_probe());
    const auto text = out.str();
    if (const auto failure =
            expect(contains(text, "Default render stream"),
                   "Expected probe label")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Frames per block: 128"),
                   "Expected probe frames per block")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "Mode: endpoint"),
                                    "Expected probe endpoint mode")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Default period 100ns: 100000"),
                   "Expected probe default period")) {
      return failure;
    }
  }

  {
    std::ostringstream out;
    sar::tools::print_wasapi_runtime_summary(out, make_summary());
    const auto text = out.str();
    if (const auto failure =
            expect(contains(text,
                            "wasapi_runtime_summary health=degraded "
                            "reason_code=render_wait_timeout"),
                   "Expected machine-readable runtime summary")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Health: degraded"),
                   "Expected runtime health")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "First error code: first_error"),
                   "Expected first error code")) {
      return failure;
    }
    if (const auto failure = expect(
            contains(text, "first_error_native_hresult=0x80004005") &&
                contains(text,
                         "first_error_native_win32_code=0x00000005") &&
                contains(text,
                         "First error native HRESULT: 0x80004005") &&
                contains(text,
                         "First error native Win32 code: 0x00000005"),
            "Expected fixed-width native error reporting")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Has capture stream: yes"),
                   "Expected capture stream presence")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Render frames per block: 128"),
                   "Expected render frames per block")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture default period 100ns: 110000"),
                   "Expected capture default period")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Last stop wait us: 1500"),
                   "Expected runtime stop wait")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "capture_fifo_fill_frames=144") &&
                       contains(text, "render_fifo_fill_frames=96") &&
                       contains(text, "capture_fifo_overflow_cycles=2") &&
                       contains(text, "capture_fifo_overflow_frames=48") &&
                       contains(text, "render_fifo_overflow_cycles=3") &&
                       contains(text, "render_fifo_overflow_frames=72") &&
                       contains(text, "render_fifo_underflow_cycles=4") &&
                       contains(text, "render_fifo_underflow_frames=24"),
                   "Expected machine-readable runtime FIFO diagnostics")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture FIFO fill frames: 144") &&
                       contains(text, "Render FIFO fill frames: 96") &&
                       contains(text, "Render FIFO underflow frames: 24"),
                   "Expected readable runtime FIFO diagnostics")) {
      return failure;
    }
    if (const auto failure = expect(
            contains(text, "render_recovery_silence_episodes=6") &&
                contains(text, "Render recovery silence episodes: 6"),
            "Expected runtime recovery silence episode count")) {
      return failure;
    }
  }

  {
    auto summary = make_summary();
    summary.first_error_native_hresult.reset();
    summary.first_error_native_win32_code.reset();
    std::ostringstream out;
    sar::tools::print_wasapi_runtime_summary(out, summary);
    const auto text = out.str();
    if (const auto failure = expect(
            contains(text, "first_error_native_hresult=none") &&
                contains(text, "first_error_native_win32_code=none") &&
                contains(text, "First error native HRESULT: none") &&
                contains(text, "First error native Win32 code: none"),
            "Expected explicit missing native error reporting")) {
      return failure;
    }
  }

  {
    std::ostringstream out;
    sar::tools::print_wasapi_stream_diagnostics(
        out, "Capture stream diagnostics", make_stream_diagnostics());
    const auto text = out.str();
    if (const auto failure =
            expect(contains(text, "Capture stream diagnostics"),
                   "Expected stream diagnostics label")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text,
                            "wasapi_stream_diagnostics state=started "
                            "direction=capture mode=loopback sample_rate=44100"),
                   "Expected machine-readable stream diagnostics")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "State: started"),
                   "Expected stream diagnostics state")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Direction: capture"),
                   "Expected stream diagnostics direction")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "Mode: loopback"),
                                    "Expected stream diagnostics loopback mode")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Sample rate: 44100"),
                   "Expected stream diagnostics sample rate")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "bits_per_sample=32"),
                   "Expected stream diagnostics bit depth token")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "valid_bits_per_sample=24"),
                   "Expected stream diagnostics valid bit depth token")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "sample_format=ieee_float"),
                   "Expected stream diagnostics sample format token")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Bits per sample: 32"),
                   "Expected stream diagnostics bit depth")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Valid bits per sample: 24"),
                   "Expected stream diagnostics valid bit depth")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Sample format: ieee_float"),
                   "Expected stream diagnostics sample format")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Minimum period 100ns: 40000"),
                   "Expected stream diagnostics minimum period")) {
      return failure;
    }
  }

  {
    std::ostringstream out;
    sar::tools::print_wasapi_worker_stats(out, make_stats());
    const auto text = out.str();
    if (const auto failure =
            expect(contains(text,
                            "wasapi_worker_stats loop_cycles=10 "
                            "graph_processed_cycles=9 idle_cycles=1"),
                   "Expected machine-readable worker stats")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "capture_wait_timeout_cycles=5"),
                   "Expected machine-readable capture timeout count")) {
      return failure;
    }
    if (const auto failure = expect(
            contains(text, "duplex_event_wait_timeout_cycles=7"),
            "Expected machine-readable duplex event timeout count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "render_partial_frames=10"),
                   "Expected machine-readable render partial frames")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "last_stop_wait_us=1500"),
                   "Expected machine-readable stop wait")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "xrun_count=16"),
                                    "Expected machine-readable xrun count")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "last_callback_ns=17000"),
                                    "Expected machine-readable callback duration")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "peak_callback_ns=18000"),
                                    "Expected machine-readable peak callback duration")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "total_callback_ns=153000"),
                                    "Expected machine-readable total callback duration")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "capture_resampler_input_frames=950") &&
                       contains(text, "capture_resampler_output_frames=940") &&
                       contains(text, "capture_rate_correction_ppm=12.5") &&
                       contains(text, "capture_clock_feed_forward_ppm=4.25") &&
                       contains(text, "capture_fifo_correction_ppm=8.25") &&
                       contains(text, "capture_resampler_ratio=0.999988") &&
                       contains(text, "capture_rate_adapter_active=1") &&
                       contains(text, "capture_rate_correction_clamped=1") &&
                       contains(text, "capture_rate_adapter_recovering=1") &&
                       contains(text, "capture_rate_adapter_reset_cycles=4") &&
                       contains(text, "capture_rate_clamped_cycles=21") &&
                       contains(
                           text,
                           "current_consecutive_capture_rate_clamped_cycles=6") &&
                       contains(
                           text,
                           "maximum_consecutive_capture_rate_clamped_cycles=9") &&
                       contains(
                           text,
                           "current_consecutive_capture_rate_clamped_frames=768") &&
                       contains(
                           text,
                           "maximum_consecutive_capture_rate_clamped_frames=1152") &&
                       contains(text, "render_startup_silence_cycles=2") &&
                       contains(text, "render_startup_silence_frames=256") &&
                       contains(text, "render_capture_starvation_silence_cycles=3") &&
                       contains(text, "render_capture_starvation_silence_frames=384") &&
                       contains(text, "render_recovery_silence_cycles=5") &&
                       contains(text, "render_recovery_silence_frames=640") &&
                       contains(text, "render_recovery_silence_episodes=4") &&
                       contains(
                           text,
                           "maximum_render_recovery_silence_frames=192") &&
                       contains(text, "last_render_startup_silence=1") &&
                       contains(text, "last_render_capture_starvation_silence=1") &&
                       contains(text, "last_render_recovery_silence=1") &&
                       contains(text, "minimum_capture_rate_correction_ppm=-22") &&
                       contains(text, "maximum_capture_rate_correction_ppm=17"),
                   "Expected machine-readable capture rate statistics")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "stream_wait_cancellation_cycles=15"),
                   "Expected machine-readable stream wait cancellation count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "last_graph_processed=1"),
                   "Expected machine-readable graph processed flag")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "last_render_wait_timed_out=1"),
                   "Expected machine-readable render timeout flag")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "last_capture_timestamp_error=0"),
                   "Expected machine-readable timestamp flag")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Loop cycles: 10"),
                   "Expected worker loop cycles")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture wait timeout cycles: 5"),
                   "Expected capture timeout count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Stream stop error cycles: 14"),
                   "Expected stream stop error count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Stream wait cancellation cycles: 15"),
                   "Expected stream wait cancellation count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture discontinuity cycles: 13"),
                   "Expected capture discontinuity count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture timestamp error frames: 16"),
                   "Expected capture timestamp error frames")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "Xruns: 16"),
                                    "Expected worker xrun count")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "Peak callback ns: 18000"),
                                    "Expected worker peak callback duration")) {
      return failure;
    }
    if (const auto failure = expect(contains(text, "Total callback ns: 153000"),
                                    "Expected worker total callback duration")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Capture resampler input frames: 950") &&
                       contains(text, "Capture resampler output frames: 940") &&
                       contains(text, "Capture rate correction ppm: 12.5") &&
                       contains(text, "Capture clock feed-forward ppm: 4.25") &&
                       contains(text, "Capture FIFO correction ppm: 8.25") &&
                       contains(text, "Capture rate adapter active: yes") &&
                       contains(text, "Capture rate correction clamped: yes") &&
                       contains(text, "Capture rate adapter reset cycles: 4") &&
                       contains(text, "Capture rate clamped cycles: 21") &&
                       contains(
                           text,
                           "Current consecutive capture rate clamped cycles: 6") &&
                       contains(
                           text,
                           "Maximum consecutive capture rate clamped cycles: 9") &&
                       contains(
                           text,
                           "Current consecutive capture rate clamped frames: 768") &&
                       contains(
                           text,
                           "Maximum consecutive capture rate clamped frames: 1152") &&
                       contains(text, "Render recovery silence episodes: 4") &&
                       contains(text, "Minimum capture rate correction ppm: -22") &&
                       contains(text, "Maximum capture rate correction ppm: 17"),
                   "Expected readable capture rate statistics")) {
      return failure;
    }
  }

  {
    std::ostringstream out;
    sar::tools::print_wasapi_engine_diagnostics(out, make_diagnostics());
    const auto text = out.str();
    if (const auto failure =
            expect(contains(text, "Graph version: 42"),
                   "Expected graph version")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "Xruns: 3"),
                   "Expected xrun count")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "engine_diagnostics graph_version=42") &&
                       contains(text, "capture_fifo_fill_frames=144") &&
                       contains(text, "render_fifo_fill_frames=96") &&
                       contains(text, "capture_fifo_overflow_cycles=2") &&
                       contains(text, "capture_fifo_overflow_frames=48") &&
                       contains(text, "render_fifo_overflow_cycles=3") &&
                       contains(text, "render_fifo_overflow_frames=72") &&
                       contains(text, "render_fifo_underflow_cycles=4") &&
                       contains(text, "render_fifo_underflow_frames=24"),
                   "Expected machine-readable engine FIFO diagnostics")) {
      return failure;
    }
  }

  std::cout << "WASAPI measure report smoke test passed\n";
  return 0;
}
