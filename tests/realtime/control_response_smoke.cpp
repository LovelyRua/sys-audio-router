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

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"mic", "Mic"});
  preset.matrix.outputs.push_back({"monitor", "Monitor"});
  preset.matrix.routes.push_back({"mic", "monitor", 1.0F});
  return preset;
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
    if (const auto failure = expect(
            !response.has_wasapi_recovery,
            "Expected no WASAPI recovery payload without a runtime snapshot")) {
      return failure;
    }
  }

  {
    const auto response = sar::control::preset_response("cmd_preset", make_preset());
    if (const auto failure = expect(response.has_preset, "Expected preset payload")) {
      return failure;
    }
    if (const auto failure = expect(response.preset.matrix.routes.size() == 1,
                                    "Expected preset route payload")) {
      return failure;
    }
  }

  {
    sar::platform::AudioDeviceDescriptor device;
    device.id = "sar_asio_1";
    device.label = "SAR ASIO 1";
    device.backend = sar::platform::AudioBackendKind::VirtualAsio;
    device.direction = sar::platform::AudioDeviceDirection::Duplex;
    device.is_virtual = true;
    device.formats.push_back({
        .sample_rate = 48000,
        .channels = 16,
        .frames_per_block = 128,
    });

    const auto response = sar::control::device_list_response("cmd_devices", {device});
    if (const auto failure = expect(response.has_devices, "Expected devices payload")) {
      return failure;
    }
    if (const auto failure = expect(response.devices.size() == 1, "Expected one device")) {
      return failure;
    }
    if (const auto failure = expect(response.devices[0].id == "sar_asio_1",
                                    "Expected device ID")) {
      return failure;
    }
    if (const auto failure = expect(response.devices[0].is_virtual,
                                    "Expected virtual device")) {
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

  {
    sar::platform::AudioDeviceDescriptor device;
    device.id = "sar_asio_1";
    device.label = "SAR ASIO 1";
    device.backend = sar::platform::AudioBackendKind::VirtualAsio;
    device.direction = sar::platform::AudioDeviceDirection::Duplex;
    device.is_virtual = true;
    device.formats.push_back({
        .sample_rate = 48000,
        .channels = 2,
        .frames_per_block = 128,
    });

    auto graph_result = sar::graph::GraphBuilder(12, 2, 128)
                            .sample_rate(48000)
                            .add_node("matrix",
                                      "Main Matrix",
                                      std::make_unique<sar::graph::PassthroughNode>())
                            .build();
    if (const auto failure = expect(graph_result.ok(), "Expected state graph build success")) {
      return failure;
    }

    auto graph = graph_result.take_graph();
    const auto response = sar::control::session_state_response("cmd_state",
                                                               make_preset(),
                                                               {device},
                                                               *graph,
                                                               13);
    if (const auto failure = expect(response.has_session_state,
                                    "Expected session state payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_preset, "Expected state preset payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_devices, "Expected state devices payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_active_graph,
                                    "Expected state graph payload")) {
      return failure;
    }
    if (const auto failure = expect(response.next_graph_version == 13,
                                    "Expected state next graph version")) {
      return failure;
    }
  }

  std::cout << "Control response smoke test passed\n";
  return 0;
}
