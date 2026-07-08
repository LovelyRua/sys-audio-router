#include "core/platform/audio_device_registry.h"
#include "core/platform/mock_audio_device_provider.h"

#include <iostream>
#include <memory>
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
    sar::platform::AudioDeviceRegistry registry;
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{
            make_device("input_1", "Input 1"),
        }));
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{
            make_device("output_1", "Output 1"),
        }));
    registry.add_provider(nullptr);

    if (const auto failure = expect(registry.provider_count() == 2,
                                    "Expected null provider to be ignored")) {
      return failure;
    }

    const auto result = registry.list_devices();
    if (const auto failure = expect(result.ok(), "Expected registry list success")) {
      return failure;
    }
    if (const auto failure = expect(result.devices().size() == 2,
                                    "Expected two registry devices")) {
      return failure;
    }
  }

  {
    sar::platform::AudioDeviceRegistry registry;
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{
            make_device("dup", "Device A"),
        }));
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{
            make_device("dup", "Device B"),
        }));

    const auto result = registry.list_devices();
    if (const auto failure = expect(!result.ok(), "Expected duplicate registry failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_device_id"),
                                    "Expected duplicate_device_id error")) {
      return failure;
    }
  }

  {
    auto invalid = make_device("", "");
    invalid.formats.clear();

    sar::platform::AudioDeviceRegistry registry;
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{invalid}));

    const auto result = registry.list_devices();
    if (const auto failure = expect(!result.ok(), "Expected provider error failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_id"),
                                    "Expected propagated empty_device_id error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_formats"),
                                    "Expected propagated empty_device_formats error")) {
      return failure;
    }
  }

  {
    auto invalid = make_device("unknown_format", "Unknown Format");
    invalid.formats[0].sample_format = sar::platform::AudioSampleFormat::Unknown;

    sar::platform::AudioDeviceRegistry registry;
    registry.add_provider(std::make_unique<sar::platform::MockAudioDeviceProvider>(
        std::vector<sar::platform::AudioDeviceDescriptor>{invalid}));

    const auto result = registry.list_devices();
    if (const auto failure = expect(!result.ok(), "Expected registry sample format failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_sample_format"),
                                    "Expected propagated invalid_sample_format error")) {
      return failure;
    }
  }

  std::cout << "Audio device registry smoke test passed\n";
  return 0;
}
