#pragma once

#include "core/control/preset_document.h"
#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/audio_device.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sar::control {

enum class ControlResponseStatus {
  Accepted,
  Rejected,
};

struct ControlResponse {
  std::string command_id;
  ControlResponseStatus status = ControlResponseStatus::Accepted;
  std::vector<PresetError> errors;
  diagnostics::EngineDiagnostics diagnostics;
  bool has_diagnostics = false;
  std::vector<platform::AudioDeviceDescriptor> devices;
  bool has_devices = false;
  struct ActiveGraphNode {
    std::string id;
    std::string label;
  };
  struct ActiveGraphSummary {
    std::uint64_t version = 0;
    std::uint32_t sample_rate = 0;
    std::size_t channels = 0;
    std::size_t frames = 0;
    std::vector<ActiveGraphNode> nodes;
  };
  ActiveGraphSummary active_graph;
  bool has_active_graph = false;
};

[[nodiscard]] ControlResponse command_accepted(std::string command_id);
[[nodiscard]] ControlResponse command_rejected(std::string command_id,
                                               std::vector<PresetError> errors);
[[nodiscard]] ControlResponse diagnostics_response(
    std::string command_id,
    diagnostics::EngineDiagnostics diagnostics);
[[nodiscard]] ControlResponse device_list_response(
    std::string command_id,
    std::vector<platform::AudioDeviceDescriptor> devices);
[[nodiscard]] ControlResponse active_graph_response(std::string command_id,
                                                    const graph::Graph& graph);

}  // namespace sar::control
