#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"

#include <iostream>
#include <memory>

int main() {
  sar::realtime::AudioBuffer input(2, 64);
  sar::realtime::AudioBuffer output(2, 64);
  sar::diagnostics::EngineDiagnostics diagnostics;

  if (diagnostics.capture_fifo_fill_frames != 0 ||
      diagnostics.render_fifo_fill_frames != 0 ||
      diagnostics.capture_fifo_overflow_cycles != 0 ||
      diagnostics.capture_fifo_overflow_frames != 0 ||
      diagnostics.render_fifo_overflow_cycles != 0 ||
      diagnostics.render_fifo_overflow_frames != 0 ||
      diagnostics.render_fifo_underflow_cycles != 0 ||
      diagnostics.render_fifo_underflow_frames != 0) {
    std::cerr << "FIFO diagnostics should default to zero\n";
    return 1;
  }

  sar::graph::Graph graph(42, input.channels(), input.frames());
  graph.add_node(std::make_unique<sar::graph::PassthroughNode>());

  graph.process(input, output, diagnostics);
  graph.process(input, output, diagnostics);

  if (diagnostics.graph_version != 42) {
    std::cerr << "Unexpected graph version\n";
    return 1;
  }

  if (diagnostics.processed_blocks != 2) {
    std::cerr << "Unexpected processed block count\n";
    return 1;
  }

  if (diagnostics.peak_callback_seconds < diagnostics.last_callback_seconds) {
    std::cerr << "Peak callback time should be at least last callback time\n";
    return 1;
  }

  std::cout << "Diagnostics smoke test passed\n";
  return 0;
}
