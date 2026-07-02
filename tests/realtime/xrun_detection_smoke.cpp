#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "core/realtime/process_context.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

class SlowNode final : public sar::graph::Node {
 public:
  void process(const sar::realtime::ProcessContext& context,
               const sar::realtime::AudioBuffer& input,
               sar::realtime::AudioBuffer& output) noexcept override {
    (void)context;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    output.copy_from(input);
  }
};

}  // namespace

int main() {
  sar::realtime::AudioBuffer input(2, 16);
  sar::realtime::AudioBuffer output(2, 16);
  sar::diagnostics::EngineDiagnostics diagnostics;

  sar::graph::Graph graph(1, input.channels(), input.frames(), 48000);
  graph.add_node(std::make_unique<SlowNode>());

  graph.process(input, output, diagnostics);

  if (diagnostics.xrun_count == 0) {
    std::cerr << "Expected xrun count to increase for slow processing\n";
    return 1;
  }

  std::cout << "Xrun detection smoke test passed\n";
  return 0;
}

