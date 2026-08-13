#include "core/service/audio_runtime_topology.h"

#include <utility>

namespace sar::service {

AudioRuntimeTopologyResult AudioRuntimeTopologyResult::success(
    AudioRuntimeTopology topology) {
  return {std::move(topology), {}};
}

AudioRuntimeTopologyResult AudioRuntimeTopologyResult::failure(
    std::vector<control::PresetError> errors) {
  return {{}, std::move(errors)};
}

bool AudioRuntimeTopologyResult::ok() const noexcept {
  return errors_.empty();
}

const AudioRuntimeTopology& AudioRuntimeTopologyResult::topology() const noexcept {
  return topology_;
}

AudioRuntimeTopology AudioRuntimeTopologyResult::take_topology() noexcept {
  return std::move(topology_);
}

const std::vector<control::PresetError>& AudioRuntimeTopologyResult::errors()
    const noexcept {
  return errors_;
}

AudioRuntimeTopologyResult::AudioRuntimeTopologyResult(
    AudioRuntimeTopology topology,
    std::vector<control::PresetError> errors) noexcept
    : topology_(std::move(topology)), errors_(std::move(errors)) {}

AudioRuntimeTopologyResult build_audio_runtime_topology(
    const control::AudioRuntimeConfiguration& configuration) {
  auto errors =
      control::validate_audio_runtime_configuration(configuration, false);
  if (!errors.empty()) {
    return AudioRuntimeTopologyResult::failure(std::move(errors));
  }

  AudioRuntimeTopology topology;
  if (configuration.mode == control::AudioRuntimeMode::WasapiMatrix) {
    topology.endpoints = configuration.endpoints;
    for (std::size_t index = 0; index < topology.endpoints.size(); ++index) {
      if (topology.endpoints[index].clock_master) {
        topology.clock_master_index = index;
        break;
      }
    }
    return AudioRuntimeTopologyResult::success(std::move(topology));
  }

  if (configuration.mode == control::AudioRuntimeMode::WasapiDuplex) {
    topology.endpoints.push_back({
        "wasapi.capture",
        configuration.capture_device_id,
        control::AudioRuntimeEndpointDirection::Capture,
        false,
        0,
        0,
    });
  }
  topology.clock_master_index = topology.endpoints.size();
  topology.endpoints.push_back({
      "wasapi.render",
      configuration.render_device_id,
      control::AudioRuntimeEndpointDirection::Render,
      true,
      0,
      0,
  });
  return AudioRuntimeTopologyResult::success(std::move(topology));
}

}  // namespace sar::service
