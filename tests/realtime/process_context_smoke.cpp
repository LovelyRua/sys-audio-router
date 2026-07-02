#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "core/realtime/process_context.h"

#include <iostream>
#include <memory>

namespace {

class ContextProbeNode final : public sar::graph::Node {
 public:
  void process(const sar::realtime::ProcessContext& context,
               const sar::realtime::AudioBuffer& input,
               sar::realtime::AudioBuffer& output) noexcept override {
    last_context = context;
    output.copy_from(input);
  }

  sar::realtime::ProcessContext last_context;
};

}  // namespace

int main() {
  sar::realtime::AudioBuffer input(2, 32);
  sar::realtime::AudioBuffer output(2, 32);
  sar::diagnostics::EngineDiagnostics diagnostics;

  auto probe = std::make_unique<ContextProbeNode>();
  const auto* probe_view = probe.get();

  sar::graph::Graph graph(7, input.channels(), input.frames(), 96000);
  graph.add_node(std::move(probe));

  graph.process(input, output, diagnostics);
  graph.process(input, output, diagnostics);

  if (probe_view->last_context.sample_rate != 96000) {
    std::cerr << "Unexpected sample rate in process context\n";
    return 1;
  }

  if (probe_view->last_context.frames != 32) {
    std::cerr << "Unexpected frame count in process context\n";
    return 1;
  }

  if (probe_view->last_context.block_index != 1) {
    std::cerr << "Unexpected block index in process context\n";
    return 1;
  }

  std::cout << "Process context smoke test passed\n";
  return 0;
}

