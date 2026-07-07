#include "core/control/control_response.h"

#include "core/graph/graph_builder.h"
#include "core/graph/node.h"

#include <iostream>
#include <memory>

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
  {
    const auto response = sar::control::command_accepted("cmd_1");
    if (const auto failure = expect(response.command_id == "cmd_1",
                                    "Expected accepted command ID")) {
      return failure;
    }
    if (const auto failure =
            expect(response.status == sar::control::ControlResponseStatus::Accepted,
                   "Expected accepted status")) {
      return failure;
    }
    if (const auto failure = expect(response.errors.empty(), "Expected no accepted errors")) {
      return failure;
    }
    if (const auto failure = expect(!response.has_diagnostics,
                                    "Expected no diagnostics on plain accepted response")) {
      return failure;
    }
  }

  {
    const auto response = sar::control::command_rejected(
        "cmd_2",
        {
            {"invalid_route_gain", "Route gain must be finite."},
        });
    if (const auto failure =
            expect(response.status == sar::control::ControlResponseStatus::Rejected,
                   "Expected rejected status")) {
      return failure;
    }
    if (const auto failure = expect(response.errors.size() == 1, "Expected one error")) {
      return failure;
    }
    if (const auto failure = expect(response.errors[0].code == "invalid_route_gain",
                                    "Expected rejected error code")) {
      return failure;
    }
  }

  {
    sar::diagnostics::EngineDiagnostics diagnostics;
    diagnostics.graph_version = 7;
    diagnostics.xrun_count = 3;
    const auto response = sar::control::diagnostics_response("cmd_3", diagnostics);
    if (const auto failure = expect(response.has_diagnostics,
                                    "Expected diagnostics payload")) {
      return failure;
    }
    if (const auto failure = expect(response.diagnostics.graph_version == 7,
                                    "Expected diagnostics graph version")) {
      return failure;
    }
    if (const auto failure = expect(response.diagnostics.xrun_count == 3,
                                    "Expected diagnostics xrun count")) {
      return failure;
    }
  }

  {
    auto graph_result = sar::graph::GraphBuilder(11, 2, 128)
                            .sample_rate(48000)
                            .add_node("monitor_gain",
                                      "Monitor Gain",
                                      std::make_unique<sar::graph::GainNode>(0.5F))
                            .build();
    if (const auto failure = expect(graph_result.ok(), "Expected graph build success")) {
      return failure;
    }

    auto graph = graph_result.take_graph();
    const auto response = sar::control::active_graph_response("cmd_4", *graph);
    if (const auto failure = expect(response.has_active_graph,
                                    "Expected active graph payload")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.version == 11,
                                    "Expected active graph version")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.sample_rate == 48000,
                                    "Expected active graph sample rate")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.channels == 2,
                                    "Expected active graph channel count")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.frames == 128,
                                    "Expected active graph frame count")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.nodes.size() == 1,
                                    "Expected active graph node summary")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.nodes[0].id == "monitor_gain",
                                    "Expected active graph node ID")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.nodes[0].label == "Monitor Gain",
                                    "Expected active graph node label")) {
      return failure;
    }
  }

  std::cout << "Control response smoke test passed\n";
  return 0;
}
