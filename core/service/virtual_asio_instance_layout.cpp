#include "core/service/virtual_asio_instance_layout.h"

#include <limits>

namespace sar::service {

std::optional<VirtualAsioInstanceLayout> virtual_asio_instance_layout(
    const std::vector<control::VirtualAsioDeviceDefinition>& definitions,
    const VirtualAsioMatrixProfile& matrix_profile) noexcept {
  if (matrix_profile.channels == 0) {
    return std::nullopt;
  }

  VirtualAsioInstanceLayout result;
  result.instances.reserve(definitions.size());
  for (std::size_t index = 0; index < definitions.size(); ++index) {
    const auto& definition = definitions[index];
    if (!definition.enabled) {
      continue;
    }
    if (definition.input_channels == 0 || definition.output_channels == 0 ||
        definition.input_channels >
            std::numeric_limits<std::size_t>::max() - result.input_channels ||
        definition.output_channels >
            std::numeric_limits<std::size_t>::max() - result.output_channels) {
      return std::nullopt;
    }
    result.instances.push_back({
        .definition_index = index,
        .daw_output_offset = result.output_channels,
        .daw_input_offset = result.input_channels,
        .output_channels = definition.output_channels,
        .input_channels = definition.input_channels,
    });
    result.output_channels += definition.output_channels;
    result.input_channels += definition.input_channels;
  }

  if (result.instances.empty() ||
      result.output_channels != matrix_profile.channels ||
      result.input_channels != matrix_profile.channels) {
    return std::nullopt;
  }
  return result;
}

}  // namespace sar::service
