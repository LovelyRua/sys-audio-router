#pragma once

#include "core/control/session_document.h"
#include "core/service/virtual_asio_matrix_profile.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace sar::service {

struct VirtualAsioInstanceSlice {
  std::size_t definition_index = 0;
  std::size_t daw_output_offset = 0;
  std::size_t daw_input_offset = 0;
  std::size_t output_channels = 0;
  std::size_t input_channels = 0;
};

struct VirtualAsioInstanceLayout {
  std::vector<VirtualAsioInstanceSlice> instances;
  std::size_t output_channels = 0;
  std::size_t input_channels = 0;
};

// Compiles enabled device definitions into immutable channel slices relative
// to the matrix Virtual ASIO groups. The current matrix profile is symmetric,
// but individual devices may expose asymmetric input/output channel counts.
[[nodiscard]] std::optional<VirtualAsioInstanceLayout>
virtual_asio_instance_layout(
    const std::vector<control::VirtualAsioDeviceDefinition>& definitions,
    const VirtualAsioMatrixProfile& matrix_profile) noexcept;

}  // namespace sar::service
