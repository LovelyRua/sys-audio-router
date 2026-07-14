#pragma once

#include "core/platform/audio_device.h"
#include "core/platform/wasapi_endpoint_selection_policy.h"

namespace sar::platform {

class WindowsWasapiDeviceProvider final : public AudioDeviceProvider {
 public:
  [[nodiscard]] AudioBackendKind backend() const noexcept override;
  [[nodiscard]] AudioDeviceListResult list_devices() const override;
  [[nodiscard]] WasapiEndpointResolutionResult resolve_endpoint(
      const WasapiEndpointSelectionPolicy& policy,
      WasapiEndpointDirection direction) const;
  [[nodiscard]] WasapiEndpointPairResolutionResult resolve_endpoint_pair(
      const WasapiEndpointSelectionPolicy& policy) const;
};

}  // namespace sar::platform
