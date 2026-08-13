#pragma once

#include "core/control/preset_document.h"

#include <cstddef>
#include <optional>

namespace sar::service {

struct VirtualAsioMatrixProfile {
  std::size_t daw_output_offset = 0;
  std::size_t daw_input_offset = 0;
  std::size_t channels = 0;
};

[[nodiscard]] std::optional<VirtualAsioMatrixProfile>
virtual_asio_matrix_profile(const control::PresetRouteMatrix& matrix) noexcept;

[[nodiscard]] bool resize_virtual_asio_matrix_profile(
    control::PresetDocument& preset,
    std::size_t channels);

}  // namespace sar::service
