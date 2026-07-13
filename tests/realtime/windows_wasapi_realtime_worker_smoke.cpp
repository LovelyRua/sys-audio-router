#include "core/platform/windows_wasapi_realtime_worker.h"

#include "core/graph/node.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
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

sar::platform::WasapiStreamProbe make_adaptive_probe(
    sar::platform::WasapiStreamDirection direction) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.mode = sar::platform::WasapiStreamMode::Endpoint;
  probe.buffer_frames = 64;
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 1;
  probe.mix_format.frames_per_block = 64;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.valid_bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return probe;
}

std::vector<float> adaptive_samples(std::uint32_t first_frame) {
  std::vector<float> result(64);
  for (std::uint32_t frame = 0; frame < result.size(); ++frame) {
    result[frame] = static_cast<float>(first_frame + frame) / 320.0F;
  }
  return result;
}

}  // namespace

int main() {
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
    if (const auto failure = expect(stats.capture_discontinuity_cycles == 0 &&
                                        stats.capture_discontinuity_frames == 0,
                                    "Expected graph-only worker without discontinuities")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_timestamp_error_cycles == 0 &&
                                        stats.capture_timestamp_error_frames == 0,
                                    "Expected graph-only worker without timestamp errors")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_resampler_input_frames == 0 &&
                                        stats.capture_resampler_output_frames == 0,
                                    "Expected graph-only worker without resampler frames")) {
      return failure;
    }
    if (const auto failure = expect(!stats.capture_rate_adapter_active &&
                                        stats.capture_rate_correction_ppm == 0.0 &&
                                        stats.capture_resampler_ratio == 1.0 &&
                                        stats.capture_rate_adapter_reset_cycles == 0 &&
                                        stats.minimum_capture_rate_correction_ppm == 0.0 &&
                                        stats.maximum_capture_rate_correction_ppm == 0.0,
                                    "Expected graph-only worker default capture rate stats")) {
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
    if (const auto failure = expect(!stats.last_capture_discontinuity &&
                                        !stats.last_capture_timestamp_error,
                                    "Expected graph-only worker last cycle without capture errors")) {
      return failure;
    }
    if (const auto failure = expect(stats.process_error_cycles == 0,
                                    "Expected graph-only worker without process errors")) {
      return failure;
    }
    if (const auto failure = expect(stats.xrun_count == diagnostics.xrun_count,
                                    "Expected worker xrun snapshot to match diagnostics")) {
      return failure;
    }
    if (const auto failure = expect(stats.last_callback_nanoseconds > 0,
                                    "Expected worker callback duration")) {
      return failure;
    }
    if (const auto failure = expect(stats.peak_callback_nanoseconds >=
                                        stats.last_callback_nanoseconds,
                                    "Expected peak callback duration to cover last callback")) {
      return failure;
    }
    if (const auto failure =
            expect(stats.total_callback_nanoseconds >=
                       stats.last_callback_nanoseconds,
                   "Expected total callback duration to cover last callback")) {
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
    if (const auto failure =
            expect(worker.stats().capture_resampler_input_frames == 0 &&
                       worker.stats().capture_resampler_output_frames == 0 &&
                       !worker.stats().capture_rate_adapter_active &&
                       worker.stats().capture_rate_correction_ppm == 0.0 &&
                       worker.stats().capture_resampler_ratio == 1.0 &&
                       worker.stats().capture_rate_adapter_reset_cycles == 0 &&
                       worker.stats().minimum_capture_rate_correction_ppm == 0.0 &&
                       worker.stats().maximum_capture_rate_correction_ppm == 0.0,
                   "Expected restart to reset capture rate stats")) {
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
    sar::tests::ScriptedWasapiStream capture(
        make_adaptive_probe(sar::platform::WasapiStreamDirection::Capture));
    sar::tests::ScriptedWasapiStream render(
        make_adaptive_probe(sar::platform::WasapiStreamDirection::Render));
    for (std::uint32_t packet = 0; packet < 5; ++packet) {
      capture.enqueue_capture(
          {.frames = 64, .samples = {adaptive_samples(packet * 64)}});
    }
    capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
    render.enqueue_render({.writable_frames = 64});
    render.enqueue_render({.writable_frames = 64});

    sar::platform::WindowsWasapiGraphRunner runner(
        &capture, &render, 1, 1, 64, 64, 64, 256, true, true);
    sar::graph::Graph graph(15, 1, 64, 48000);
    graph.add_node(std::make_unique<sar::graph::PassthroughNode>());
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);

    const auto start_result = worker.start(1);
    if (const auto failure =
            expect(start_result.ok(), "Expected adaptive capture worker start")) {
      return failure;
    }
    for (int attempt = 0; attempt < 100 && worker.running(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.stop();

    const auto stats = worker.stats();
    if (const auto failure = expect(stats.capture_resampler_input_frames > 64,
                                    "Expected accumulated resampler input frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_resampler_output_frames == 128,
                                    "Expected accumulated resampler output frames")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_rate_adapter_active,
                                    "Expected last capture rate adapter state")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_rate_correction_ppm > 0.0,
                                    "Expected last capture rate correction")) {
      return failure;
    }
    if (const auto failure = expect(stats.capture_resampler_ratio < 1.0 &&
                                        std::isfinite(stats.capture_resampler_ratio),
                                    "Expected last capture resampler ratio")) {
      return failure;
    }
    if (const auto failure =
            expect(stats.capture_rate_adapter_reset_cycles == 0 &&
                       stats.minimum_capture_rate_correction_ppm == 0.0 &&
                       stats.maximum_capture_rate_correction_ppm >=
                           stats.capture_rate_correction_ppm,
                   "Expected capture rate range without adapter resets")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 2, 16);
    sar::graph::Graph graph(14, 2, 16);
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);
    std::atomic_int ready = 0;
    std::atomic_bool go = false;
    bool first_started = false;
    bool second_started = false;
    const auto start_worker = [&](bool& started) {
      ready.fetch_add(1);
      while (!go.load()) {
        std::this_thread::yield();
      }
      started = worker.start(0).ok();
    };
    std::thread first(start_worker, std::ref(first_started));
    std::thread second(start_worker, std::ref(second_started));
    while (ready.load() != 2) {
      std::this_thread::yield();
    }
    go.store(true);
    first.join();
    second.join();
    if (const auto failure = expect(first_started != second_started,
                                    "Expected exactly one concurrent start to succeed")) {
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
    sar::tests::ScriptedWasapiStream capture(
        make_adaptive_probe(sar::platform::WasapiStreamDirection::Capture));
    capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
    capture.set_stop_result(sar::platform::WasapiStreamResult::failure({
        {"synthetic_stop_failed", "Synthetic capture stream stop failure."},
    }));

    sar::platform::WindowsWasapiGraphRunner runner(&capture, nullptr, 1, 64);
    sar::graph::Graph graph(16, 1, 64, 48000);
    graph.add_node(std::make_unique<sar::graph::PassthroughNode>());
    sar::diagnostics::EngineDiagnostics diagnostics;
    sar::platform::WindowsWasapiRealtimeWorker worker(runner, graph, diagnostics);

    const auto start_result = worker.start(1);
    if (const auto failure =
            expect(start_result.ok(), "Expected stop-failure worker start success")) {
      return failure;
    }
    for (int attempt = 0; attempt < 100 && worker.running(); ++attempt) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    worker.stop();

    if (const auto failure = expect(!worker.running(),
                                    "Expected stop-failure worker to finish")) {
      return failure;
    }
    if (const auto failure = expect(capture.stop_calls() == 1,
                                    "Expected one capture stream stop attempt")) {
      return failure;
    }
    if (const auto failure = expect(
            has_error_code(worker.last_errors(), "synthetic_stop_failed"),
            "Expected worker to preserve the stream stop failure")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().stream_stop_error_cycles == 1,
                                    "Expected one stream stop error cycle")) {
      return failure;
    }
    if (const auto failure = expect(worker.stats().stream_wait_cancellation_cycles == 1,
                                    "Expected bounded cancellation before stop failure")) {
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
            expect(!start_result.ok(), "Expected closed-stream worker start failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(start_result.errors(), "stream_not_open"),
                                    "Expected synchronous stream_not_open result")) {
      return failure;
    }
    if (const auto failure = expect(!worker.running(),
                                    "Expected failed startup worker to be stopped")) {
      return failure;
    }

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
