#include "core/platform/audio_device_registry.h"

#include <utility>

namespace sar::platform {

void AudioDeviceRegistry::add_provider(std::unique_ptr<AudioDeviceProvider> provider) {
  if (provider != nullptr) {
    providers_.push_back(std::move(provider));
  }
}

std::size_t AudioDeviceRegistry::provider_count() const noexcept {
  return providers_.size();
}

AudioDeviceListResult AudioDeviceRegistry::list_devices() const {
  std::vector<AudioDeviceDescriptor> devices;
  std::vector<AudioDeviceError> errors;

  for (const auto& provider : providers_) {
    auto result = provider->list_devices();
    if (!result.ok()) {
      for (const auto& error : result.errors()) {
        errors.push_back(error);
      }
      continue;
    }

    for (const auto& device : result.devices()) {
      devices.push_back(device);
    }
  }

  auto validation_errors = validate_audio_devices(devices);
  for (const auto& error : validation_errors) {
    errors.push_back(error);
  }

  if (!errors.empty()) {
    return AudioDeviceListResult::failure(std::move(errors));
  }
  return AudioDeviceListResult::success(std::move(devices));
}

}  // namespace sar::platform
