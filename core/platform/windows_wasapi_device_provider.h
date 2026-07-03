#pragma once

#include "core/platform/audio_device.h"

namespace sar::platform {

class WindowsWasapiDeviceProvider final : public AudioDeviceProvider {
 public:
  [[nodiscard]] AudioBackendKind backend() const noexcept override;
  [[nodiscard]] AudioDeviceListResult list_devices() const override;
};

}  // namespace sar::platform
