#include "core/platform/windows_wasapi_realtime_worker.h"

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
    if (const auto failure =
            expect(render_stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected worker to stop synthetic render stream")) {
      return failure;
    }
  }

  std::cout << "Windows WASAPI realtime worker smoke test passed\n";
  return 0;
}
