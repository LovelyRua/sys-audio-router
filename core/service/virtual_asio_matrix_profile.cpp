#include "core/service/virtual_asio_matrix_profile.h"

#include <string_view>

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

}  // namespace sar::service
