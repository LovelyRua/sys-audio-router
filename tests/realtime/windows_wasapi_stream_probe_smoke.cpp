#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <iostream>

namespace {

bool has_default_output_device() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return false;
  }

  for (const auto& device : result.devices()) {
    if (device.direction == sar::platform::AudioDeviceDirection::Output &&
        device.is_default) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  if (!has_default_output_device()) {
    std::cout << "Windows WASAPI stream probe skipped: no default output endpoint\n";
    return 0;
  }

  const auto result = sar::platform::probe_default_wasapi_stream(
      sar::platform::WasapiStreamDirection::Render);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  const auto& probe = result.probe();
  if (probe.device_id.empty() || probe.device_label.empty()) {
    std::cerr << "Expected probed WASAPI device identity\n";
    return 1;
  }
  if (probe.mix_format.sample_rate == 0 || probe.mix_format.channels == 0) {
    std::cerr << "Expected probed WASAPI mix format\n";
    return 1;
  }
  if (probe.default_period_100ns == 0 || probe.buffer_frames == 0) {
    std::cerr << "Expected probed WASAPI timing contract\n";
    return 1;
  }

  std::cout << "Windows WASAPI stream probe smoke test passed: "
            << probe.mix_format.sample_rate << " Hz, "
            << probe.mix_format.channels << " channels, "
            << probe.buffer_frames << " buffer frames\n";
  return 0;
}
