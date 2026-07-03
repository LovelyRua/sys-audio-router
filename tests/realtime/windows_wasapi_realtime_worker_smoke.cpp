#include "core/platform/windows_wasapi_realtime_worker.h"

#include "core/graph/node.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
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
  if (const auto failure = expect(worker.last_errors().empty(),
                                  "Expected no worker errors")) {
    return failure;
  }
  if (const auto failure = expect(diagnostics.processed_blocks >= 1,
                                  "Expected diagnostics blocks")) {
    return failure;
  }

  std::cout << "Windows WASAPI realtime worker smoke test passed\n";
  return 0;
}
