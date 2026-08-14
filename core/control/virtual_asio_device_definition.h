#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sar::control {

inline constexpr std::size_t kMaximumVirtualAsioDevices = 16;

struct VirtualAsioDeviceDefinition {
  std::string device_id;
  std::string clsid;
  std::string registry_name;
  std::string broker_token;
  std::uint32_t input_channels = 2;
  std::uint32_t output_channels = 2;
  bool enabled = true;
};

}  // namespace sar::control
