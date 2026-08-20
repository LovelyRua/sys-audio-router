#pragma once

#include "driver/windows_virtual_wasapi/virtual_wasapi_transport_abi.h"

#include <cstddef>
#include <cstdint>

namespace sar::platform {

struct VirtualWasapiTransportConfig {
  std::uint32_t direction = SAR_VWASAPI_DIRECTION_RENDER;
  std::uint32_t sample_format = SAR_VWASAPI_SAMPLE_IEEE_FLOAT;
  std::uint32_t sample_rate = 48000;
  std::uint32_t channel_count = 2;
  std::uint32_t bits_per_sample = 32;
  std::uint32_t valid_bits_per_sample = 32;
  std::uint32_t frames_per_slot = 128;
  std::uint32_t slot_count = 8;
};

enum class VirtualWasapiLayoutError {
  None,
  NullHeader,
  UnsupportedVersion,
  InvalidDirection,
  InvalidFormat,
  InvalidRange,
  ArithmeticOverflow,
  SizeMismatch,
  MisalignedOffset,
  OverlappingRegion,
  ReservedFieldNotZero,
};

struct VirtualWasapiLayoutResult {
  VirtualWasapiLayoutError error = VirtualWasapiLayoutError::None;
  SarVirtualWasapiTransportHeader header{};

  [[nodiscard]] bool ok() const noexcept { return error == VirtualWasapiLayoutError::None; }
};

[[nodiscard]] VirtualWasapiLayoutResult calculate_virtual_wasapi_transport_layout(
    const VirtualWasapiTransportConfig& config) noexcept;

[[nodiscard]] VirtualWasapiLayoutError validate_virtual_wasapi_transport_layout(
    const SarVirtualWasapiTransportHeader* header,
    std::size_t mapped_bytes,
    std::uint32_t expected_direction = 0) noexcept;

}  // namespace sar::platform
