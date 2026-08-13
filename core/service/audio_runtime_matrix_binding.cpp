#include "core/service/audio_runtime_matrix_binding.h"

#include <algorithm>
#include <utility>

namespace sar::service {

AudioRuntimeMatrixBindingResult AudioRuntimeMatrixBindingResult::success(
    std::vector<AudioRuntimeEndpointGraphBinding> bindings) {
  return {std::move(bindings), {}};
}

AudioRuntimeMatrixBindingResult AudioRuntimeMatrixBindingResult::failure(
    std::vector<control::PresetError> errors) {
  return {{}, std::move(errors)};
}

bool AudioRuntimeMatrixBindingResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<AudioRuntimeEndpointGraphBinding>&
AudioRuntimeMatrixBindingResult::bindings() const noexcept {
  return bindings_;
}

std::vector<AudioRuntimeEndpointGraphBinding>
AudioRuntimeMatrixBindingResult::take_bindings() noexcept {
  return std::move(bindings_);
}

const std::vector<control::PresetError>&
AudioRuntimeMatrixBindingResult::errors() const noexcept {
  return errors_;
}

AudioRuntimeMatrixBindingResult::AudioRuntimeMatrixBindingResult(
    std::vector<AudioRuntimeEndpointGraphBinding> bindings,
    std::vector<control::PresetError> errors) noexcept
    : bindings_(std::move(bindings)), errors_(std::move(errors)) {}

AudioRuntimeMatrixBindingResult bind_audio_runtime_to_matrix(
    const AudioRuntimeTopology& topology,
    const control::PresetRouteMatrix& matrix) {
  std::vector<AudioRuntimeEndpointGraphBinding> bindings;
  std::vector<control::PresetError> errors;
  bindings.reserve(topology.endpoints.size());

  for (const auto& endpoint : topology.endpoints) {
    const auto& axis = endpoint.direction ==
                               control::AudioRuntimeEndpointDirection::Capture
                           ? matrix.inputs
                           : matrix.outputs;
    std::size_t first = 0;
    bool complete = true;
    for (std::size_t channel = 0; channel < endpoint.channel_count; ++channel) {
      const auto port_id =
          endpoint.endpoint_id + ".ch" + std::to_string(channel + 1);
      const auto found = std::ranges::find_if(
          axis, [&port_id](const auto& port) { return port.id == port_id; });
      if (found == axis.end()) {
        errors.push_back({
            "audio_runtime_matrix_port_missing",
            "Matrix does not contain runtime port '" + port_id + "'.",
        });
        complete = false;
        continue;
      }
      const auto index = static_cast<std::size_t>(found - axis.begin());
      if (channel == 0) {
        first = index;
      } else if (index != first + channel) {
        errors.push_back({
            "audio_runtime_matrix_port_range_not_contiguous",
            "Runtime endpoint '" + endpoint.endpoint_id +
                "' must occupy contiguous matrix channels in native order.",
        });
        complete = false;
        break;
      }
    }
    if (complete) {
      bindings.push_back({endpoint.endpoint_id, endpoint.direction, first,
                          endpoint.channel_count, endpoint.clock_master});
    }
  }

  if (!errors.empty()) {
    return AudioRuntimeMatrixBindingResult::failure(std::move(errors));
  }
  return AudioRuntimeMatrixBindingResult::success(std::move(bindings));
}

}  // namespace sar::service
