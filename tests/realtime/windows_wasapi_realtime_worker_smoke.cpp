#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"

#include "core/graph/node.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool has_error_code(const std::vector<sar::platform::WasapiRealtimeWorkerError>& errors,
                    const std::string& code) {
  for (const auto& error : errors) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

sar::platform::WasapiStreamDiagnostics make_stream_diagnostics(
    sar::platform::WasapiStreamDirection direction,
    std::uint32_t buffer_frames) {
  sar::platform::WasapiStreamDiagnostics diagnostics;
  diagnostics.state = sar::platform::WasapiStreamState::Started;
  diagnostics.direction = direction;
  diagnostics.mix_format.sample_rate = 48000;
  diagnostics.mix_format.channels = 2;
  diagnostics.mix_format.frames_per_block = buffer_frames;
  diagnostics.mix_format.bits_per_sample = 32;
  diagnostics.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  diagnostics.buffer_frames = buffer_frames;
  diagnostics.default_period_100ns = 100000;
  diagnostics.minimum_period_100ns = 30000;
  return diagnostics;
}

sar::platform::WasapiStreamProbe make_render_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = sar::platform::WasapiStreamDirection::Render;
  probe.device_id = "device";
  probe.device_label = "Device";
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.mix_format.frames_per_block = 16;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = 16;
  return probe;
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

    sar::platform::WasapiRealtimeWorkerStats stats;
    auto summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Stopped,
                   "Expected no-cycle runtime summary to be stopped")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "no_cycles",
                   "Expected no-cycle runtime summary reason")) {
      return failure;
    }

    stats.loop_cycles = 8;
    stats.graph_processed_cycles = 8;
    stats.last_graph_processed = true;
    const auto capture_diagnostics =
        make_stream_diagnostics(sar::platform::WasapiStreamDirection::Capture, 96);
    const auto render_diagnostics =
        make_stream_diagnostics(sar::platform::WasapiStreamDirection::Render, 128);
    summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, &render_diagnostics);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Healthy,
                   "Expected fully processed runtime summary to be healthy")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "running",
                   "Expected healthy runtime summary reason")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.has_capture_stream && summary.has_render_stream,
                   "Expected runtime summary stream presence")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_buffer_frames == 96 &&
                       summary.render_buffer_frames == 128,
                   "Expected runtime summary buffer sizes")) {
      return failure;
    }

    stats.wait_timeout_cycles = 1;
    stats.render_wait_timeout_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, &render_diagnostics);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected timeout runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "render_wait_timeout",
                   "Expected split timeout runtime summary reason")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.render_wait_timeout_cycles == 1,
                   "Expected runtime summary render timeout count")) {
      return failure;
    }

    stats.render_wait_timeout_cycles = 0;
    stats.capture_wait_timeout_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, &render_diagnostics);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected capture timeout runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "capture_wait_timeout",
                   "Expected capture timeout runtime summary reason")) {
      return failure;
    }

    stats.render_wait_timeout_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(
        stats, {}, &capture_diagnostics, &render_diagnostics);
    if (const auto failure =
            expect(summary.reason_code == "wait_timeout",
                   "Expected combined timeout runtime summary reason")) {
      return failure;
    }

    stats.wait_timeout_cycles = 0;
    stats.capture_wait_timeout_cycles = 0;
    stats.render_wait_timeout_cycles = 0;
    stats.capture_partial_cycles = 1;
    stats.capture_partial_frames = 4;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected partial runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "capture_partial_buffer",
                   "Expected split partial runtime summary reason")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_partial_frames == 4,
                   "Expected runtime summary capture partial frame count")) {
      return failure;
    }

    stats.capture_partial_cycles = 0;
    stats.capture_partial_frames = 0;
    stats.render_partial_cycles = 1;
    stats.render_partial_frames = 8;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected render partial runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "render_partial_buffer",
                   "Expected split render partial runtime summary reason")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.render_partial_frames == 8,
                   "Expected runtime summary render partial frame count")) {
      return failure;
    }

    stats.capture_partial_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.reason_code == "partial_buffer",
                   "Expected combined partial runtime summary reason")) {
      return failure;
    }

    stats.capture_partial_cycles = 0;
    stats.capture_partial_frames = 0;
    stats.render_partial_cycles = 0;
    stats.render_partial_frames = 0;
    stats.capture_silent_cycles = 1;
    stats.capture_silent_frames = 16;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected silent capture runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "silent_capture",
                   "Expected silent capture runtime summary reason")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.capture_silent_frames == 16,
                   "Expected runtime summary silent frame count")) {
      return failure;
    }

    stats.capture_silent_cycles = 0;
    stats.capture_silent_frames = 0;
    stats.idle_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Degraded,
                   "Expected idle runtime summary to be degraded")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "idle_cycle",
                   "Expected idle runtime summary reason")) {
      return failure;
    }
    stats.idle_cycles = 0;

    stats.stream_start_error_cycles = 1;
    stats.process_error_cycles = 1;
    stats.stream_stop_error_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Faulted,
                   "Expected start-error runtime summary to be faulted")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "stream_start_error",
                   "Expected start-error priority in runtime summary")) {
      return failure;
    }
    stats.stream_start_error_cycles = 0;

    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.reason_code == "process_error",
                   "Expected process-error priority in runtime summary")) {
      return failure;
    }
    stats.process_error_cycles = 0;

    stats.stream_stop_error_cycles = 1;
    summary = sar::platform::summarize_wasapi_runtime(stats, {}, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Faulted,
                   "Expected stop-error runtime summary to be faulted")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "stream_stop_error",
                   "Expected stop-error runtime summary reason")) {
      return failure;
    }
    stats.stream_stop_error_cycles = 0;

    const std::vector<sar::platform::WasapiRealtimeWorkerError> errors = {
        {"native_stream_unavailable", "Synthetic stream has no native WASAPI client."},
        {"ignored_second_error", "Second error should not be the primary reason."},
    };
    summary = sar::platform::summarize_wasapi_runtime(stats, errors, nullptr, nullptr);
    if (const auto failure =
            expect(summary.health == sar::platform::WasapiRuntimeHealth::Faulted,
                   "Expected error runtime summary to be faulted")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.error_count == 2,
                   "Expected runtime summary error count")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.first_error_code == "native_stream_unavailable",
                   "Expected runtime summary first error code")) {
      return failure;
    }
    if (const auto failure =
            expect(summary.reason_code == "native_stream_unavailable",
                   "Expected runtime summary reason to use first error")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 2, 16);
    auto& input = runner.input_buffer();
    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      auto samples = input.channel(channel);
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        samples[frame] = 0.25F;
      }
    }

    sar::graph::Graph graph(11, 2, 16);
    graph.add_node(std::make_unique<sar::graph::GainNode>(0.5F));
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);

    const auto start_result = worker.start(0);
    if (const auto failure = expect(start_result.ok(), "Expected worker start success")) {
      return failure;
    }

    for (int attempt = 0; attempt < 100 && worker.processed_cycles() < 5; ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    worker.stop();
    if (const auto failure = expect(!worker.running(), "Expected stopped worker")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().last_stop_wait_microseconds < 1000000,
                                    "Expected bounded worker stop wait")) {
      return failure;
    }
    if (const auto failure = expect(worker.processed_cycles() >= 1,
                                    "Expected processed worker cycles")) {
      return failure;
    }
    const auto stats = worker.stats();
    if (const auto failure = expect(stats.loop_cycles >= stats.graph_processed_cycles,
                                    "Expected loop cycles to include graph cycles")) {
      return failure;
    }
    if (const auto failure = expect(stats.graph_processed_cycles == worker.processed_cycles(),
                                    "Expected stats graph cycles to match processed cycles")) {
      return failure;
    }
    if (const auto failure = expect(stats.wait_timeout_cycles == 0,
                                    "Expected graph-only worker without wait timeouts")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_idle_cycles == 0,
                                    "Expected graph-only worker without capture idle cycles")) {
      return failure;
    }
    if (const auto failure = expect(stats.render_idle_cycles == 0,
                                    "Expected graph-only worker without render idle cycles")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_wait_timeout_cycles == 0,
                                    "Expected graph-only worker without capture timeouts")) {
      return failure;
    }
    if (const auto failure = expect(stats.render_wait_timeout_cycles == 0,
                                    "Expected graph-only worker without render timeouts")) {
      return failure;
    }
    if (const auto failure =
            expect(stats.wait_timeout_cycles <= stats.capture_wait_timeout_cycles +
                                                stats.render_wait_timeout_cycles,
                   "Expected total wait timeout cycles to be covered by split counters")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_partial_cycles == 0,
                                    "Expected graph-only worker without partial capture")) {
      return failure;
    }
    if (const auto failure = expect(stats.render_partial_cycles == 0,
                                    "Expected graph-only worker without partial render")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_partial_frames == 0,
                                    "Expected graph-only worker without partial capture frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.render_partial_frames == 0,
                                    "Expected graph-only worker without partial render frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_silent_cycles == 0,
                                    "Expected graph-only worker without silent capture cycles")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_silent_frames == 0,
                                    "Expected graph-only worker without silent capture frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.stream_start_error_cycles == 0,
                                    "Expected graph-only worker without stream start errors")) {
      return failure;
    }
    if (const auto failure = expect(stats.stream_stop_error_cycles == 0,
                                    "Expected graph-only worker without stream stop errors")) {
      return failure;
    }
    if (const auto failure = expect(stats.last_captured_frames == 0,
                                    "Expected graph-only worker without last captured frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.last_rendered_frames == 0,
                                    "Expected graph-only worker without last rendered frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.last_graph_processed,
                                    "Expected graph-only worker last cycle processed")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_capture_idle,
                                    "Expected graph-only worker last cycle without capture idle")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_render_idle,
                                    "Expected graph-only worker last cycle without render idle")) {
      return failure;
    }
    if (const auto failure =
            expect(!stats.last_capture_wait_timed_out,
                   "Expected graph-only worker last cycle without capture timeout")) {
      return failure;
    }
    if (const auto failure =
            expect(!stats.last_render_wait_timed_out,
                   "Expected graph-only worker last cycle without render timeout")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_capture_partial,
                                    "Expected graph-only worker last cycle without capture partial")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_render_partial,
                                    "Expected graph-only worker last cycle without render partial")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_capture_silent,
                                    "Expected graph-only worker last cycle without silent capture")) {
      return failure;
    }
    if (const auto failure = expect(stats.process_error_cycles == 0,
                                    "Expected graph-only worker without process errors")) {
      return failure;
    }
    if (const auto failure = expect(worker.last_errors().empty(),
                                    "Expected no worker errors")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.processed_blocks >= 1,
                                    "Expected diagnostics blocks")) {
      return failure;
    }

    const auto restart_result = worker.start(0);
    if (const auto failure = expect(restart_result.ok(), "Expected worker restart success")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().last_stop_wait_microseconds == 0,
                                    "Expected restart to reset stop wait")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().last_captured_frames == 0,
                                    "Expected restart to reset last captured frames")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().last_rendered_frames == 0,
                                    "Expected restart to reset last rendered frames")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().stream_start_error_cycles == 0,
                                    "Expected restart to reset stream start errors")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().stream_stop_error_cycles == 0,
                                    "Expected restart to reset stream stop errors")) {
      return failure;
    }
    const auto duplicate_start = worker.start(0);
    if (const auto failure = expect(!duplicate_start.ok(),
                                    "Expected duplicate worker start failure")) {
      return failure;
    }
    worker.stop();
  }

  {
    sar::platform::WindowsWasapiStream render_stream;
    const auto open_result = render_stream.open(make_render_probe());
    if (const auto failure = expect(open_result.ok(), "Expected synthetic render open")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 16);
    sar::graph::Graph graph(12, 2, 16);
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);

    const auto start_result = worker.start(0);
    if (const auto failure =
            expect(start_result.ok(), "Expected synthetic render worker start")) {
      return failure;
    }

    for (int attempt = 0; attempt < 100 && worker.running(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.stop();

    const auto errors = worker.last_errors();
    if (const auto failure = expect(has_error_code(errors, "native_stream_unavailable"),
                                    "Expected native_stream_unavailable worker error")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().process_error_cycles == 1,
                                    "Expected one worker process error")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().stream_stop_error_cycles == 0,
                                    "Expected synthetic render stop to succeed")) {
      return failure;
    }
    if (const auto failure =
            expect(render_stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected worker to stop synthetic render stream")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream render_stream;
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 16);
    sar::graph::Graph graph(13, 2, 16);
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);

    const auto start_result = worker.start(0);
    if (const auto failure =
            expect(start_result.ok(), "Expected closed-stream worker thread start")) {
      return failure;
    }

    for (int attempt = 0; attempt < 100 && worker.running(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.stop();

    const auto errors = worker.last_errors();
    if (const auto failure = expect(has_error_code(errors, "stream_not_open"),
                                    "Expected stream_not_open worker error")) {
      return failure;
    }
    const auto stats = worker.stats();
    if (const auto failure = expect(stats.stream_start_error_cycles == 1,
                                    "Expected one stream start error")) {
      return failure;
    }
    if (const auto failure = expect(stats.process_error_cycles == 0,
                                    "Expected no process errors after stream start failure")) {
      return failure;
    }
    if (const auto failure = expect(stats.stream_stop_error_cycles == 0,
                                    "Expected no stop errors after stream start failure")) {
      return failure;
    }
    if (const auto failure = expect(stats.loop_cycles == 0,
                                    "Expected no loop cycles after stream start failure")) {
      return failure;
    }
    if (const auto failure = expect(stats.graph_processed_cycles == 0,
                                    "Expected no graph cycles after stream start failure")) {
      return failure;
    }
    if (const auto failure = expect(!stats.last_graph_processed,
                                    "Expected no last graph cycle after stream start failure")) {
      return failure;
    }
  }

  std::cout << "Windows WASAPI realtime worker smoke test passed\n";
  return 0;
}
