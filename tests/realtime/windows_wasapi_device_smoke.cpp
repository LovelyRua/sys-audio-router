#include "core/platform/windows_wasapi_device_provider.h"

#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

int verify_device_contract(const sar::platform::AudioDeviceDescriptor& device) {
  if (const auto failure = expect(device.backend == sar::platform::AudioBackendKind::Wasapi,
                                  "Expected WASAPI device backend")) {
    return failure;
  }
  if (const auto failure = expect(!device.id.empty(), "Expected WASAPI device id")) {
    return failure;
  }
  if (const auto failure = expect(!device.label.empty(), "Expected WASAPI device label")) {
    return failure;
  }
  if (const auto failure = expect(!device.formats.empty(), "Expected WASAPI device format")) {
    return failure;
  }

  for (const auto& format : device.formats) {
    if (const auto failure = expect(format.sample_rate > 0,
                                    "Expected WASAPI device sample rate")) {
      return failure;
    }
    if (const auto failure = expect(format.channels > 0,
                                    "Expected WASAPI device channel count")) {
      return failure;
    }
    if (const auto failure = expect(format.frames_per_block > 0,
                                    "Expected WASAPI device block size")) {
      return failure;
    }
    if (const auto failure = expect(format.bits_per_sample > 0,
                                    "Expected WASAPI device bit depth")) {
      return failure;
    }
    if (const auto failure =
            expect(format.sample_format != sar::platform::AudioSampleFormat::Unknown,
                   "Expected WASAPI device sample format")) {
      return failure;
    }
    if (const auto failure =
            expect(format.valid_bits_per_sample <= format.bits_per_sample,
                   "Expected WASAPI valid bits not to exceed container bits")) {
      return failure;
    }
  }

  return 0;
}

}  // namespace

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

  bool saw_default_input = false;
  bool saw_default_output = false;
  for (const auto& device : result.devices()) {
    if (const auto failure = verify_device_contract(device)) {
      return failure;
    }
    if (device.is_default && device.direction == sar::platform::AudioDeviceDirection::Input) {
      saw_default_input = true;
    }
    if (device.is_default && device.direction == sar::platform::AudioDeviceDirection::Output) {
      saw_default_output = true;
    }
  }

  std::cout << "Windows WASAPI device smoke test passed with "
            << result.devices().size() << " active endpoints";
  if (saw_default_input) {
    std::cout << ", default input";
  }
  if (saw_default_output) {
    std::cout << ", default output";
  }
  std::cout << '\n';
  return 0;
}
