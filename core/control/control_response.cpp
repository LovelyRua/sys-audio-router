#include "core/control/control_response.h"

#include <utility>

namespace sar::control {

ControlResponse command_accepted(std::string command_id) {
  ControlResponse response;
  response.command_id = std::move(command_id);
  response.status = ControlResponseStatus::Accepted;
  return response;
}

ControlResponse command_rejected(std::string command_id,
                                 std::vector<PresetError> errors) {
  ControlResponse response;
  response.command_id = std::move(command_id);
  response.status = ControlResponseStatus::Rejected;
  response.errors = std::move(errors);
  return response;
}

ControlResponse preset_response(std::string command_id, PresetDocument preset) {
  auto response = command_accepted(std::move(command_id));
  response.preset = std::move(preset);
  response.has_preset = true;
  return response;
}

ControlResponse diagnostics_response(std::string command_id,
                                     diagnostics::EngineDiagnostics diagnostics) {
  auto response = command_accepted(std::move(command_id));
  response.diagnostics = diagnostics;
  response.has_diagnostics = true;
  return response;
}

ControlResponse device_list_response(
    std::string command_id,
    std::vector<platform::AudioDeviceDescriptor> devices) {
  auto response = command_accepted(std::move(command_id));
  response.devices = std::move(devices);
  response.has_devices = true;
  return response;
}

ControlResponse active_graph_response(std::string command_id,
                                      const graph::Graph& graph) {
  auto response = command_accepted(std::move(command_id));
  response.active_graph.version = graph.version();
  response.active_graph.sample_rate = graph.sample_rate();
  response.active_graph.channels = graph.channels();
  response.active_graph.frames = graph.frames();
  response.active_graph.nodes.reserve(graph.node_count());

  for (std::size_t index = 0; index < graph.node_count(); ++index) {
    response.active_graph.nodes.push_back({
        std::string{graph.node_id(index)},
        std::string{graph.node_label(index)},
    });
  }

  response.has_active_graph = true;
  return response;
}

ControlResponse session_state_response(
    std::string command_id,
    PresetDocument preset,
    std::vector<platform::AudioDeviceDescriptor> devices,
    const graph::Graph& graph,
    std::uint64_t next_graph_version) {
  auto response = active_graph_response(std::move(command_id), graph);
  response.preset = std::move(preset);
  response.has_preset = true;
  response.devices = std::move(devices);
  response.has_devices = true;
  response.next_graph_version = next_graph_version;
  response.has_session_state = true;
  return response;
}

ControlResponse audio_runtime_state_response(std::string command_id,
                                             bool installed,
                                             bool running,
                                             std::uint64_t graph_version) {
  auto response = command_accepted(std::move(command_id));
  response.audio_runtime.installed = installed;
  response.audio_runtime.running = running;
  response.audio_runtime.graph_version = graph_version;
  response.has_audio_runtime_state = true;
  return response;
}

}  // namespace sar::control
