#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/graph_snapshot.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <memory>

namespace {

constexpr float kFirstGain = 0.5F;
constexpr float kSecondGain = 0.25F;

std::shared_ptr<sar::graph::Graph> make_graph(std::uint64_t version,
                                              std::size_t channels,
                                              std::size_t frames,
                                              float gain) {
  auto graph = std::make_shared<sar::graph::Graph>(version, channels, frames);
  graph->add_node(std::make_unique<sar::graph::GainNode>(gain));
  return graph;
}

}  // namespace

int main() {
  sar::realtime::AudioBuffer input(2, 128);
  sar::realtime::AudioBuffer output(2, 128);
  sar::diagnostics::EngineDiagnostics diagnostics;

  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = static_cast<float>(frame + 1);
    }
  }

  sar::graph::GraphSnapshotPublisher publisher(
      make_graph(1, input.channels(), input.frames(), kFirstGain));

  publisher.process(input, output, diagnostics);
  if (diagnostics.graph_version != 1) {
    std::cerr << "Expected graph version 1\n";
    return 1;
  }

  publisher.publish(make_graph(2, input.channels(), input.frames(), kSecondGain));
  publisher.process(input, output, diagnostics);

  if (diagnostics.graph_version != 2) {
    std::cerr << "Expected graph version 2 after publish\n";
    return 1;
  }

  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    const auto in = input.channel(channel);
    const auto out = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame) {
      if (!sar::tests::nearly_equal(out[frame], in[frame] * kSecondGain)) {
        return sar::tests::fail_sample("Unexpected sample after graph publish",
                                       channel,
                                       frame);
      }
    }
  }

  std::cout << "Graph snapshot smoke test passed. processed_blocks="
            << diagnostics.processed_blocks << '\n';
  return 0;
}
