#include "core/service/virtual_asio_matrix_profile.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_set>

namespace sar::service {

namespace {

struct AxisRange {
  std::size_t offset = 0;
  std::size_t count = 0;
  bool fragmented = false;
};

AxisRange find_range(const std::vector<graph::RouteEndpointDescriptor>& ports,
                     std::string_view prefix) noexcept {
  AxisRange result;
  bool inside = false;
  bool ended = false;
  for (std::size_t index = 0; index < ports.size(); ++index) {
    const bool matches = ports[index].id.starts_with(prefix);
    if (matches && ended) {
      result.fragmented = true;
      return result;
    }
    if (matches) {
      if (!inside) {
        result.offset = index;
        inside = true;
      }
      ++result.count;
    } else if (inside) {
      ended = true;
    }
  }
  return result;
}

}  // namespace

std::optional<VirtualAsioMatrixProfile> virtual_asio_matrix_profile(
    const control::PresetRouteMatrix& matrix) noexcept {
  const auto outputs_from_daw = find_range(matrix.inputs, "asio-output-");
  const auto inputs_to_daw = find_range(matrix.outputs, "asio-input-");
  if (outputs_from_daw.fragmented || inputs_to_daw.fragmented ||
      outputs_from_daw.count == 0 ||
      outputs_from_daw.count != inputs_to_daw.count) {
    return std::nullopt;
  }
  return VirtualAsioMatrixProfile{
      outputs_from_daw.offset,
      inputs_to_daw.offset,
      outputs_from_daw.count,
  };
}

bool resize_virtual_asio_matrix_profile(control::PresetDocument& preset,
                                        std::size_t channels) {
  const auto profile = virtual_asio_matrix_profile(preset.matrix);
  if (!profile.has_value() || channels == 0) {
    return false;
  }

  auto inputs = preset.matrix.inputs;
  auto outputs = preset.matrix.outputs;
  inputs.erase(inputs.begin() + profile->daw_output_offset,
               inputs.begin() + profile->daw_output_offset + profile->channels);
  outputs.erase(outputs.begin() + profile->daw_input_offset,
                outputs.begin() + profile->daw_input_offset + profile->channels);
  for (std::size_t channel = 0; channel < channels; ++channel) {
    const auto number = std::to_string(channel + 1);
    inputs.insert(inputs.begin() + profile->daw_output_offset + channel,
                  {"asio-output-" + number, "ASIO DAW Out " + number});
    outputs.insert(outputs.begin() + profile->daw_input_offset + channel,
                   {"asio-input-" + number, "ASIO DAW In " + number});
  }

  std::unordered_set<std::string> input_ids;
  std::unordered_set<std::string> output_ids;
  for (const auto& input : inputs) input_ids.insert(input.id);
  for (const auto& output : outputs) output_ids.insert(output.id);
  std::erase_if(preset.matrix.routes, [&](const auto& route) {
    return !input_ids.contains(route.input_id) ||
           !output_ids.contains(route.output_id);
  });
  preset.matrix.inputs = std::move(inputs);
  preset.matrix.outputs = std::move(outputs);
  return true;
}

}  // namespace sar::service
