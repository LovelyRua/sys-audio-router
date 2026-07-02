#pragma once

#include "core/platform/audio_device.h"

#include <string>
#include <vector>

namespace sar::platform {

struct VirtualEndpointDescriptor {
  std::string id;
  std::string label;
  AudioBackendKind backend = AudioBackendKind::VirtualAsio;
  AudioDeviceDirection direction = AudioDeviceDirection::Duplex;
  AudioFormat format;
};

struct VirtualEndpointError {
  std::string code;
  std::string message;
};

class VirtualEndpointResult {
 public:
  static VirtualEndpointResult success();
  static VirtualEndpointResult failure(std::vector<VirtualEndpointError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<VirtualEndpointError>& errors() const noexcept;

 private:
  explicit VirtualEndpointResult(std::vector<VirtualEndpointError> errors);

  std::vector<VirtualEndpointError> errors_;
};

class VirtualEndpointRegistry {
 public:
  [[nodiscard]] VirtualEndpointResult add_endpoint(VirtualEndpointDescriptor endpoint);
  [[nodiscard]] VirtualEndpointResult remove_endpoint(const std::string& id);

  [[nodiscard]] const std::vector<VirtualEndpointDescriptor>& endpoints() const noexcept;
  [[nodiscard]] AudioDeviceListResult list_devices() const;

 private:
  std::vector<VirtualEndpointDescriptor> endpoints_;
};

[[nodiscard]] std::vector<VirtualEndpointError> validate_virtual_endpoint(
    const VirtualEndpointDescriptor& endpoint);

}  // namespace sar::platform
