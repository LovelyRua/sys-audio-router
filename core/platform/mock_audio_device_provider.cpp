#include "core/platform/mock_audio_device_provider.h"

#include <utility>

namespace sar::platform {

MockAudioDeviceProvider::MockAudioDeviceProvider(std::vector<AudioDeviceDescriptor> devices)
    : devices_(std::move(devices)) {}

AudioBackendKind MockAudioDeviceProvider::backend() const noexcept {
  return AudioBackendKind::Mock;
}

AudioDeviceListResult MockAudioDeviceProvider::list_devices() const {
  auto errors = validate_audio_devices(devices_);
  if (!errors.empty()) {
    return AudioDeviceListResult::failure(std::move(errors));
  }
  return AudioDeviceListResult::success(devices_);
}

}  // namespace sar::platform
