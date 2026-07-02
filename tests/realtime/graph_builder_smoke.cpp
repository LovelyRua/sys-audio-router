#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph_builder.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <memory>
#include <string_view>

namespace {

bool has_error_code(const sar::graph::GraphBuildResult& result, const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  {
    sar::graph::GraphBuilder builder(1, 2, 64);
    auto result = builder.sample_rate(48000)
                      .add_node("monitor_gain",
                                "Monitor Gain",
                                std::make_unique<sar::graph::GainNode>(0.25F))
                      .build();

    if (const auto failure = expect(result.ok(), "Expected graph builder success")) {
      return failure;
    }

    auto graph = result.take_graph();
    if (const auto failure = expect(graph != nullptr, "Expected graph pointer")) {
      return failure;
    }
    if (const auto failure = expect(graph->node_count() == 1, "Expected one graph node")) {
      return failure;
    }
    if (const auto failure = expect(graph->node_id(0) == std::string_view{"monitor_gain"},
                                    "Expected graph node ID to be preserved")) {
      return failure;
    }
    if (const auto failure = expect(graph->node_label(0) == std::string_view{"Monitor Gain"},
                                    "Expected graph node label to be preserved")) {
      return failure;
    }

    sar::realtime::AudioBuffer input(2, 64);
    sar::realtime::AudioBuffer output(2, 64);
    sar::diagnostics::EngineDiagnostics diagnostics;

    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      auto samples = input.channel(channel);
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        samples[frame] = 4.0F;
      }
    }

    graph->process(input, output, diagnostics);
    for (std::size_t channel = 0; channel < output.channels(); ++channel) {
      const auto samples = output.channel(channel);
      for (std::size_t frame = 0; frame < output.frames(); ++frame) {
        if (!sar::tests::nearly_equal(samples[frame], 1.0F)) {
          return sar::tests::fail_sample("Unexpected builder graph output", channel, frame);
        }
      }
    }
  }

  {
    auto result = sar::graph::GraphBuilder(0, 0, 0).sample_rate(0).build();
    if (const auto failure = expect(!result.ok(), "Expected invalid graph builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_version"),
                                    "Expected invalid_version error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_channels"),
                                    "Expected invalid_channels error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_frames"),
                                    "Expected invalid_frames error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_sample_rate"),
                                    "Expected invalid_sample_rate error")) {
      return failure;
    }
  }

  {
    auto result = sar::graph::GraphBuilder(1, 2, 64).add_node(nullptr).build();
    if (const auto failure = expect(!result.ok(), "Expected null node builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "null_node"),
                                    "Expected null_node error")) {
      return failure;
    }
  }

  {
    auto result = sar::graph::GraphBuilder(1, 2, 64)
                      .add_node("", "Monitor", std::make_unique<sar::graph::PassthroughNode>())
                      .build();
    if (const auto failure = expect(!result.ok(), "Expected empty ID builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_node_id"),
                                    "Expected empty_node_id error")) {
      return failure;
    }
  }

  {
    auto result = sar::graph::GraphBuilder(1, 2, 64)
                      .add_node("monitor", "", std::make_unique<sar::graph::PassthroughNode>())
                      .build();
    if (const auto failure = expect(!result.ok(), "Expected empty label builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_node_label"),
                                    "Expected empty_node_label error")) {
      return failure;
    }
  }

  {
    auto result = sar::graph::GraphBuilder(1, 2, 64)
                      .add_node("dup", "Input", std::make_unique<sar::graph::PassthroughNode>())
                      .add_node("dup", "Output", std::make_unique<sar::graph::GainNode>(1.0F))
                      .build();
    if (const auto failure = expect(!result.ok(), "Expected duplicate ID builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_node_id"),
                                    "Expected duplicate_node_id error")) {
      return failure;
    }
  }

  {
    auto result = sar::graph::GraphBuilder(1, 2, 64)
                      .add_node("input", "dup", std::make_unique<sar::graph::PassthroughNode>())
                      .add_node("output", "dup", std::make_unique<sar::graph::GainNode>(1.0F))
                      .build();
    if (const auto failure = expect(!result.ok(), "Expected duplicate label builder failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_node_label"),
                                    "Expected duplicate_node_label error")) {
      return failure;
    }
  }

  std::cout << "Graph builder smoke test passed\n";
  return 0;
}
