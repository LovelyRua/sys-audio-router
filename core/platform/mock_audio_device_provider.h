#pragma once

#include "core/platform/audio_device.h"

#include <vector>

namespace sar::platform {

class MockAudioDeviceProvider final : public AudioDeviceProvider {
 public:
  explicit MockAudioDeviceProvider(std::vector<AudioDeviceDescriptor> devices);

  [[nodiscard]] AudioBackendKind backend() const noexcept override;
  [[nodiscard]] AudioDeviceListResult list_devices() const override;

 private:
  std::vector<AudioDeviceDescriptor> devices_;
};

}  // namespace sar::platform
