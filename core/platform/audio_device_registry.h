#pragma once

#include "core/platform/audio_device.h"

#include <memory>
#include <vector>

namespace sar::platform {

class AudioDeviceRegistry {
 public:
  void add_provider(std::unique_ptr<AudioDeviceProvider> provider);

  [[nodiscard]] std::size_t provider_count() const noexcept;
  [[nodiscard]] AudioDeviceListResult list_devices() const;

 private:
  std::vector<std::unique_ptr<AudioDeviceProvider>> providers_;
};

}  // namespace sar::platform
