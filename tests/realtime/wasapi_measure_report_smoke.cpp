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
  return summary;
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
  stats.capture_partial_cycles = 7;
  stats.capture_partial_frames = 8;
  stats.render_partial_cycles = 9;
  stats.render_partial_frames = 10;
  stats.capture_silent_cycles = 11;
  stats.capture_silent_frames = 12;
  stats.captured_frames = 960;
  stats.rendered_frames = 1280;
  stats.last_captured_frames = 96;
  stats.last_rendered_frames = 128;
  stats.stream_start_error_cycles = 13;
  stats.stream_stop_error_cycles = 14;
  stats.process_error_cycles = 15;
  stats.last_stop_wait_microseconds = 1500;
  return stats;
}

sar::diagnostics::EngineDiagnostics make_diagnostics() {
  sar::diagnostics::EngineDiagnostics diagnostics;
  diagnostics.graph_version = 42;
  diagnostics.processed_blocks = 99;
  diagnostics.xrun_count = 3;
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
            expect(contains(text, "Health: degraded"),
                   "Expected runtime health")) {
      return failure;
    }
    if (const auto failure =
            expect(contains(text, "First error code: first_error"),
                   "Expected first error code")) {
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
  }

  {
    std::ostringstream out;
    sar::tools::print_wasapi_worker_stats(out, make_stats());
    const auto text = out.str();
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
  }

  std::cout << "WASAPI measure report smoke test passed\n";
  return 0;
}
