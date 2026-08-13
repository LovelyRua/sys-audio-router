#pragma once

#include "core/control/control_command.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sar::service {

struct AudioRuntimePortDescriptor {
  std::string port_id;
  std::string endpoint_id;
  control::AudioRuntimeEndpointDirection direction =
      control::AudioRuntimeEndpointDirection::Capture;
  std::uint32_t native_channel = 0;
  bool clock_master = false;
};

struct AudioRuntimeTopology {
  std::vector<control::AudioRuntimeEndpointConfiguration> endpoints;
  std::vector<AudioRuntimePortDescriptor> ports;
  std::size_t clock_master_index = 0;
};

class AudioRuntimeTopologyResult {
 public:
  static AudioRuntimeTopologyResult success(AudioRuntimeTopology topology);
  static AudioRuntimeTopologyResult failure(
      std::vector<control::PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const AudioRuntimeTopology& topology() const noexcept;
  [[nodiscard]] AudioRuntimeTopology take_topology() noexcept;
  [[nodiscard]] const std::vector<control::PresetError>& errors() const noexcept;

 private:
  AudioRuntimeTopologyResult(AudioRuntimeTopology topology,
                             std::vector<control::PresetError> errors) noexcept;

  AudioRuntimeTopology topology_;
  std::vector<control::PresetError> errors_;
};

// Normalizes legacy one-pair configurations into the same stable logical-port
// shape used by matrix mode. Opening devices remains a separate control-plane
// phase so this function is deterministic and platform independent.
[[nodiscard]] AudioRuntimeTopologyResult build_audio_runtime_topology(
    const control::AudioRuntimeConfiguration& configuration);

}  // namespace sar::service
