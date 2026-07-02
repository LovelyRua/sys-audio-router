#include "core/platform/virtual_endpoint.h"

#include <algorithm>
#include <utility>

namespace sar::platform {

namespace {

bool is_virtual_backend(AudioBackendKind backend) noexcept {
  return backend == AudioBackendKind::VirtualAsio ||
         backend == AudioBackendKind::VirtualWasapi;
}

AudioDeviceDescriptor to_audio_device(const VirtualEndpointDescriptor& endpoint) {
  AudioDeviceDescriptor device;
  device.id = endpoint.id;
  device.label = endpoint.label;
  device.backend = endpoint.backend;
  device.direction = endpoint.direction;
  device.formats.push_back(endpoint.format);
  device.is_virtual = true;
  return device;
}

}  // namespace

VirtualEndpointResult VirtualEndpointResult::success() {
  return VirtualEndpointResult({});
}

VirtualEndpointResult VirtualEndpointResult::failure(std::vector<VirtualEndpointError> errors) {
  return VirtualEndpointResult(std::move(errors));
}

bool VirtualEndpointResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<VirtualEndpointError>& VirtualEndpointResult::errors() const noexcept {
  return errors_;
}

VirtualEndpointResult::VirtualEndpointResult(std::vector<VirtualEndpointError> errors)
    : errors_(std::move(errors)) {}

std::vector<VirtualEndpointError> validate_virtual_endpoint(
    const VirtualEndpointDescriptor& endpoint) {
  std::vector<VirtualEndpointError> errors;

  if (endpoint.id.empty()) {
    errors.push_back({"empty_endpoint_id", "Virtual endpoint IDs must not be empty."});
  }
  if (endpoint.label.empty()) {
    errors.push_back({"empty_endpoint_label", "Virtual endpoint labels must not be empty."});
  }
  if (!is_virtual_backend(endpoint.backend)) {
    errors.push_back({"invalid_virtual_backend", "Virtual endpoints require a virtual backend."});
  }
  if (endpoint.format.sample_rate == 0) {
    errors.push_back({"invalid_sample_rate", "Virtual endpoint sample rate must be non-zero."});
  }
  if (endpoint.format.channels == 0) {
    errors.push_back({"invalid_channel_count", "Virtual endpoint channel count must be non-zero."});
  }
  if (endpoint.format.frames_per_block == 0) {
    errors.push_back({"invalid_frames_per_block", "Virtual endpoint block size must be non-zero."});
  }

  return errors;
}

VirtualEndpointResult VirtualEndpointRegistry::add_endpoint(
    VirtualEndpointDescriptor endpoint) {
  auto errors = validate_virtual_endpoint(endpoint);
  if (!errors.empty()) {
    return VirtualEndpointResult::failure(std::move(errors));
  }

  const auto duplicate = std::ranges::find_if(endpoints_, [&](const auto& existing) {
    return existing.id == endpoint.id;
  });
  if (duplicate != endpoints_.end()) {
    return VirtualEndpointResult::failure({
        {"duplicate_endpoint_id", "Virtual endpoint IDs must be unique."},
    });
  }

  endpoints_.push_back(std::move(endpoint));
  return VirtualEndpointResult::success();
}

VirtualEndpointResult VirtualEndpointRegistry::remove_endpoint(const std::string& id) {
  const auto endpoint = std::ranges::find_if(endpoints_, [&](const auto& existing) {
    return existing.id == id;
  });
  if (endpoint == endpoints_.end()) {
    return VirtualEndpointResult::failure({
        {"unknown_endpoint_id", "Virtual endpoint was not found."},
    });
  }

  endpoints_.erase(endpoint);
  return VirtualEndpointResult::success();
}

const std::vector<VirtualEndpointDescriptor>& VirtualEndpointRegistry::endpoints()
    const noexcept {
  return endpoints_;
}

AudioDeviceListResult VirtualEndpointRegistry::list_devices() const {
  std::vector<AudioDeviceDescriptor> devices;
  devices.reserve(endpoints_.size());
  for (const auto& endpoint : endpoints_) {
    devices.push_back(to_audio_device(endpoint));
  }

  auto errors = validate_audio_devices(devices);
  if (!errors.empty()) {
    return AudioDeviceListResult::failure(std::move(errors));
  }
  return AudioDeviceListResult::success(std::move(devices));
}

}  // namespace sar::platform
