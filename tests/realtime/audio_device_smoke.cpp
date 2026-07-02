#include "core/platform/mock_audio_device_provider.h"

#include <iostream>
#include <string>

namespace {

bool has_error_code(const sar::platform::AudioDeviceListResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::AudioDeviceDescriptor make_device(std::string id, std::string label) {
  sar::platform::AudioDeviceDescriptor device;
  device.id = std::move(id);
  device.label = std::move(label);
  device.backend = sar::platform::AudioBackendKind::Mock;
  device.direction = sar::platform::AudioDeviceDirection::Duplex;
  device.formats.push_back({
      .sample_rate = 48000,
      .channels = 2,
      .frames_per_block = 128,
  });
  return device;
}

}  // namespace

int main() {
  {
    sar::platform::MockAudioDeviceProvider provider({
        make_device("mock_input_1", "Mock Input 1"),
        make_device("mock_output_1", "Mock Output 1"),
    });

    if (const auto failure = expect(provider.backend() == sar::platform::AudioBackendKind::Mock,
                                    "Expected mock backend kind")) {
      return failure;
    }

    const auto result = provider.list_devices();
    if (const auto failure = expect(result.ok(), "Expected valid device list")) {
      return failure;
    }
    if (const auto failure = expect(result.devices().size() == 2, "Expected two devices")) {
      return failure;
    }
    if (const auto failure = expect(result.devices()[0].formats[0].sample_rate == 48000,
                                    "Expected sample rate to be preserved")) {
      return failure;
    }
  }

  {
    auto duplicate_a = make_device("dup", "Device A");
    auto duplicate_b = make_device("dup", "Device B");
    sar::platform::MockAudioDeviceProvider provider({duplicate_a, duplicate_b});
    const auto result = provider.list_devices();
    if (const auto failure = expect(!result.ok(), "Expected duplicate device failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_device_id"),
                                    "Expected duplicate_device_id error")) {
      return failure;
    }
  }

  {
    auto invalid = make_device("", "");
    invalid.formats[0].sample_rate = 0;
    invalid.formats[0].channels = 0;
    invalid.formats[0].frames_per_block = 0;
    sar::platform::MockAudioDeviceProvider provider({invalid});
    const auto result = provider.list_devices();
    if (const auto failure = expect(!result.ok(), "Expected invalid device failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_id"),
                                    "Expected empty_device_id error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_label"),
                                    "Expected empty_device_label error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_sample_rate"),
                                    "Expected invalid_sample_rate error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_channel_count"),
                                    "Expected invalid_channel_count error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_frames_per_block"),
                                    "Expected invalid_frames_per_block error")) {
      return failure;
    }
  }

  std::cout << "Audio device smoke test passed\n";
  return 0;
}
