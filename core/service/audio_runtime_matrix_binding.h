#pragma once

#include "core/control/preset_document.h"
#include "core/service/audio_runtime_topology.h"

#include <cstddef>
#include <string>
#include <vector>

namespace sar::service {

struct AudioRuntimeEndpointGraphBinding {
  std::string endpoint_id;
  control::AudioRuntimeEndpointDirection direction =
      control::AudioRuntimeEndpointDirection::Capture;
  std::size_t graph_first_channel = 0;
  std::size_t channel_count = 0;
  bool clock_master = false;
};

class AudioRuntimeMatrixBindingResult {
 public:
  static AudioRuntimeMatrixBindingResult success(
      std::vector<AudioRuntimeEndpointGraphBinding> bindings);
  static AudioRuntimeMatrixBindingResult failure(
      std::vector<control::PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<AudioRuntimeEndpointGraphBinding>& bindings()
      const noexcept;
  [[nodiscard]] std::vector<AudioRuntimeEndpointGraphBinding> take_bindings()
      noexcept;
  [[nodiscard]] const std::vector<control::PresetError>& errors() const noexcept;

 private:
  AudioRuntimeMatrixBindingResult(
      std::vector<AudioRuntimeEndpointGraphBinding> bindings,
      std::vector<control::PresetError> errors) noexcept;

  std::vector<AudioRuntimeEndpointGraphBinding> bindings_;
  std::vector<control::PresetError> errors_;
};

// Resolves stable runtime port IDs against the actual matrix axis. Endpoint
// channels must be present in native order and contiguous, which keeps the
// realtime side to one range copy per endpoint.
[[nodiscard]] AudioRuntimeMatrixBindingResult bind_audio_runtime_to_matrix(
    const AudioRuntimeTopology& topology,
    const control::PresetRouteMatrix& matrix);

}  // namespace sar::service
