#include "core/platform/windows_wasapi_runtime_summary.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiStreamDiagnostics make_stream_diagnostics(
    sar::platform::WasapiStreamDirection direction,
    std::uint32_t sample_rate,
    std::uint16_t channels,
    std::uint32_t buffer_frames) {
  sar::platform::WasapiStreamDiagnostics diagnostics;
  diagnostics.state = sar::platform::WasapiStreamState::Started;
  diagnostics.direction = direction;
  diagnostics.mix_format.sample_rate = sample_rate;
  diagnostics.mix_format.channels = channels;
  diagnostics.mix_format.frames_per_block = buffer_frames;
  diagnostics.mix_format.bits_per_sample = 32;
  diagnostics.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  diagnostics.buffer_frames = buffer_frames;
  diagnostics.default_period_100ns = 100000;
  diagnostics.minimum_period_100ns = 30000;
  return diagnostics;
}

sar::platform::WasapiRealtimeWorkerStats make_active_stats() {
  sar::platform::WasapiRealtimeWorkerStats stats;
  stats.loop_cycles = 8;
  stats.graph_processed_cycles = 8;
  stats.captured_frames = 256;
  stats.rendered_frames = 256;
  stats.last_captured_frames = 64;
  stats.last_rendered_frames = 64;
  stats.last_stop_wait_microseconds = 1200;
  stats.last_graph_processed = true;
  return stats;
}

int expect_summary(
    const sar::platform::WasapiRuntimeSummary& summary,
    sar::platform::WasapiRuntimeHealth health,
    const std::string& reason_code,
    const char* label) {
  if (const auto failure = expect(summary.health == health, label)) {
    return failure;
  }
  if (const auto failure = expect(summary.reason_code == reason_code, label)) {
    return failure;
  }
  if (const auto failure = expect(!summary.reason.empty(), label)) {
    return failure;
  }
  return 0;
}

}  // namespace

int main() {
  {
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_runtime_health_name(
                       sar::platform::WasapiRuntimeHealth::Stopped)) == "stopped",
                   "Expected stopped health name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_runtime_health_name(
                       sar::platform::WasapiRuntimeHealth::Healthy)) == "healthy",
                   "Expected healthy health name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_runtime_health_name(
                       sar::platform::WasapiRuntimeHealth::Degraded)) == "degraded",
                   "Expected degraded health name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_runtime_health_name(
                       sar::platform::WasapiRuntimeHealth::Faulted)) == "faulted",
                   "Expected faulted health name")) {
      return failure;
    }
  }

  {
    const sar::platform::WasapiRealtimeWorkerStats stats;
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Stopped,
                           "no_cycles",
                           "Expected no-cycle runtime summary")) {
      return failure;
    }
    if (const auto failure = expect(!summary.has_capture_stream,
                                    "Expected no capture stream in empty summary")) {
      return failure;
    }
    if (const auto failure = expect(!summary.has_render_stream,
                                    "Expected no render stream in empty summary")) {
      return failure;
    }
    if (const auto failure = expect(summary.error_count == 0,
                                    "Expected no errors in empty summary")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.idle_cycles = 1;
    stats.wait_timeout_cycles = 2;
    stats.capture_wait_timeout_cycles = 1;
    stats.render_wait_timeout_cycles = 1;
    stats.capture_partial_cycles = 3;
    stats.render_partial_cycles = 4;
    stats.capture_partial_frames = 12;
    stats.render_partial_frames = 16;
    stats.capture_silent_cycles = 5;
    stats.capture_silent_frames = 32;
    stats.process_error_cycles = 6;
    stats.stream_start_error_cycles = 7;
    stats.stream_stop_error_cycles = 8;
    stats.last_capture_idle = true;
    stats.last_render_idle = true;
    stats.last_captured_frames = 20;
    stats.last_rendered_frames = 24;
    stats.last_stop_wait_microseconds = 2500;
    stats.last_capture_wait_timed_out = true;
    stats.last_render_wait_timed_out = true;
    stats.last_capture_partial = true;
    stats.last_render_partial = true;
    stats.last_capture_silent = true;

    const auto capture_diagnostics = make_stream_diagnostics(
        sar::platform::WasapiStreamDirection::Capture, 48000, 4, 96);
    const auto render_diagnostics = make_stream_diagnostics(
        sar::platform::WasapiStreamDirection::Render, 48000, 2, 128);
    const auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, &render_diagnostics);

    if (const auto failure = expect(summary.loop_cycles == 8,
                                    "Expected copied loop cycle count")) {
      return failure;
    }
    if (const auto failure = expect(summary.graph_processed_cycles == 8,
                                    "Expected copied graph processed count")) {
      return failure;
    }
    if (const auto failure = expect(summary.idle_cycles == 1,
                                    "Expected copied idle count")) {
      return failure;
    }
    if (const auto failure = expect(summary.wait_timeout_cycles == 2,
                                    "Expected copied timeout count")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_wait_timeout_cycles == 1,
                                    "Expected copied capture timeout count")) {
      return failure;
    }
    if (const auto failure = expect(summary.render_wait_timeout_cycles == 1,
                                    "Expected copied render timeout count")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_partial_cycles == 3,
                                    "Expected copied capture partial count")) {
      return failure;
    }
    if (const auto failure = expect(summary.render_partial_cycles == 4,
                                    "Expected copied render partial count")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_partial_frames == 12,
                                    "Expected copied capture partial frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.render_partial_frames == 16,
                                    "Expected copied render partial frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_silent_cycles == 5,
                                    "Expected copied silent capture cycles")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_silent_frames == 32,
                                    "Expected copied silent capture frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.process_error_cycles == 6,
                                    "Expected copied process error cycles")) {
      return failure;
    }
    if (const auto failure = expect(summary.stream_start_error_cycles == 7,
                                    "Expected copied stream start error cycles")) {
      return failure;
    }
    if (const auto failure = expect(summary.stream_stop_error_cycles == 8,
                                    "Expected copied stream stop error cycles")) {
      return failure;
    }
    if (const auto failure = expect(summary.captured_frames == 256,
                                    "Expected copied captured frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.rendered_frames == 256,
                                    "Expected copied rendered frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.last_captured_frames == 20,
                                    "Expected copied last captured frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.last_rendered_frames == 24,
                                    "Expected copied last rendered frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.last_stop_wait_microseconds == 2500,
                                    "Expected copied last stop wait")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.has_capture_stream && summary.has_render_stream,
                   "Expected copied stream presence")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_sample_rate == 48000 &&
                       summary.render_sample_rate == 48000,
                   "Expected copied stream sample rates")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_channels == 4 && summary.render_channels == 2,
                   "Expected copied stream channel counts")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_frames_per_block == 96 &&
                       summary.render_frames_per_block == 128,
                   "Expected copied stream frames per block")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_buffer_frames == 96 &&
                       summary.render_buffer_frames == 128,
                   "Expected copied stream buffer frames")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_default_period_100ns == 100000 &&
                       summary.render_default_period_100ns == 100000,
                   "Expected copied default stream periods")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_minimum_period_100ns == 30000 &&
                       summary.render_minimum_period_100ns == 30000,
                   "Expected copied minimum stream periods")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.last_graph_processed && summary.last_capture_idle &&
                       summary.last_render_idle &&
                       summary.last_capture_wait_timed_out &&
                       summary.last_render_wait_timed_out &&
                       summary.last_capture_partial && summary.last_render_partial &&
                       summary.last_capture_silent,
                   "Expected copied last-cycle flags")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    const auto capture_diagnostics = make_stream_diagnostics(
        sar::platform::WasapiStreamDirection::Capture, 48000, 2, 64);
    auto summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, nullptr);
    if (const auto failure =
            expect(summary.has_capture_stream && !summary.has_render_stream,
                   "Expected capture-only stream presence")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_buffer_frames == 64,
                                    "Expected capture-only buffer frames")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_sample_rate == 48000 &&
                       summary.capture_channels == 2 &&
                       summary.capture_frames_per_block == 64,
                   "Expected capture-only stream shape")) {
      return failure;
    }
    if (const auto failure = expect(summary.render_buffer_frames == 0,
                                    "Expected no render buffer frames")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.render_sample_rate == 0 && summary.render_channels == 0 &&
                       summary.render_frames_per_block == 0,
                   "Expected no render stream shape")) {
      return failure;
    }

    const auto render_diagnostics = make_stream_diagnostics(
        sar::platform::WasapiStreamDirection::Render, 48000, 2, 128);
    summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, nullptr, &render_diagnostics);
    if (const auto failure =
            expect(!summary.has_capture_stream && summary.has_render_stream,
                   "Expected render-only stream presence")) {
      return failure;
    }
    if (const auto failure = expect(summary.capture_buffer_frames == 0,
                                    "Expected no capture buffer frames")) {
      return failure;
    }
    if (const auto failure = expect(summary.render_buffer_frames == 128,
                                    "Expected render-only buffer frames")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_sample_rate == 0 && summary.capture_channels == 0 &&
                       summary.capture_frames_per_block == 0,
                   "Expected no capture stream shape")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.render_sample_rate == 48000 &&
                       summary.render_channels == 2 &&
                       summary.render_frames_per_block == 128,
                   "Expected render-only stream shape")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Healthy,
                           "running",
                           "Expected healthy runtime summary")) {
      return failure;
    }
    if (const auto failure =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(
                       summary, false),
                   "Expected healthy summary to pass relaxed health gate")) {
      return failure;
    }
    if (const auto failure =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(
                       summary, true),
                   "Expected healthy summary to pass strict health gate")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.stream_start_error_cycles = 1;
    stats.process_error_cycles = 1;
    stats.stream_stop_error_cycles = 1;
    stats.wait_timeout_cycles = 1;
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Faulted,
                           "stream_start_error",
                           "Expected stream-start priority")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::platform::wasapi_runtime_summary_should_fail(
                       summary, false),
                   "Expected faulted summary to fail relaxed health gate")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::platform::wasapi_runtime_summary_should_fail(
                       summary, true),
                   "Expected faulted summary to fail strict health gate")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.process_error_cycles = 1;
    stats.stream_stop_error_cycles = 1;
    stats.wait_timeout_cycles = 1;
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Faulted,
                           "process_error",
                           "Expected process-error priority")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.stream_stop_error_cycles = 1;
    stats.wait_timeout_cycles = 1;
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Faulted,
                           "stream_stop_error",
                           "Expected stream-stop priority")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.wait_timeout_cycles = 1;
    stats.capture_wait_timeout_cycles = 1;
    auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "capture_wait_timeout",
                           "Expected capture timeout summary")) {
      return failure;
    }
    if (const auto failure =
            expect(!sar::platform::wasapi_runtime_summary_should_fail(
                       summary, false),
                   "Expected degraded summary to pass relaxed health gate")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::platform::wasapi_runtime_summary_should_fail(
                       summary, true),
                   "Expected degraded summary to fail strict health gate")) {
      return failure;
    }

    stats.capture_wait_timeout_cycles = 0;
    stats.render_wait_timeout_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "render_wait_timeout",
                           "Expected render timeout summary")) {
      return failure;
    }

    stats.capture_wait_timeout_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "wait_timeout",
                           "Expected combined timeout summary")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.capture_partial_cycles = 1;
    auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "capture_partial_buffer",
                           "Expected capture partial summary")) {
      return failure;
    }

    stats.capture_partial_cycles = 0;
    stats.render_partial_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "render_partial_buffer",
                           "Expected render partial summary")) {
      return failure;
    }

    stats.capture_partial_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "partial_buffer",
                           "Expected combined partial summary")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.capture_silent_cycles = 1;
    stats.idle_cycles = 1;
    auto summary =
        sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "silent_capture",
                           "Expected silent capture priority")) {
      return failure;
    }

    stats.capture_silent_cycles = 0;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Degraded,
                           "idle_cycle",
                           "Expected idle summary")) {
      return failure;
    }
  }

  {
    auto stats = make_active_stats();
    stats.stream_start_error_cycles = 1;
    const std::vector<sar::platform::WasapiRealtimeWorkerError> errors = {
        {"native_stream_unavailable", "Synthetic stream has no native WASAPI client."},
        {"ignored_later_error", "Only the first error is surfaced as the reason."},
    };
    const auto summary =
        sar::platform::summarize_wasapi_runtime(stats, errors, nullptr, nullptr);
    if (const auto failure =
            expect_summary(summary,
                           sar::platform::WasapiRuntimeHealth::Faulted,
                           "native_stream_unavailable",
                           "Expected explicit error priority")) {
      return failure;
    }
    if (const auto failure = expect(summary.error_count == 2,
                                    "Expected copied explicit error count")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.first_error_code == "native_stream_unavailable",
                   "Expected first explicit error code")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.first_error_message ==
                       "Synthetic stream has no native WASAPI client.",
                   "Expected first explicit error message")) {
      return failure;
    }
  }

  std::cout << "Windows WASAPI runtime summary smoke test passed\n";
  return 0;
}
