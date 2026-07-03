#include "core/platform/windows_wasapi_device_provider.h"

#include <iostream>

int main() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  if (provider.backend() != sar::platform::AudioBackendKind::Wasapi) {
    std::cerr << "Expected WASAPI backend kind\n";
    return 1;
  }

  const auto result = provider.list_devices();
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  for (const auto& device : result.devices()) {
    if (device.backend != sar::platform::AudioBackendKind::Wasapi) {
      std::cerr << "Expected WASAPI device backend\n";
      return 1;
    }
    if (device.id.empty() || device.label.empty() || device.formats.empty()) {
      std::cerr << "Expected populated WASAPI device descriptor\n";
      return 1;
    }
  }

  std::cout << "Windows WASAPI device smoke test passed with "
            << result.devices().size() << " active endpoints\n";
  return 0;
}
