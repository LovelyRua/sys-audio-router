#include "core/platform/audio_device.h"

#include <unordered_set>
#include <utility>

namespace sar::platform {

AudioDeviceListResult AudioDeviceListResult::success(
    std::vector<AudioDeviceDescriptor> devices) {
  return {std::move(devices), {}};
}

AudioDeviceListResult AudioDeviceListResult::failure(std::vector<AudioDeviceError> errors) {
  return {{}, std::move(errors)};
}

bool AudioDeviceListResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<AudioDeviceDescriptor>& AudioDeviceListResult::devices() const noexcept {
  return devices_;
}

const std::vector<AudioDeviceError>& AudioDeviceListResult::errors() const noexcept {
  return errors_;
}

AudioDeviceListResult::AudioDeviceListResult(std::vector<AudioDeviceDescriptor> devices,
                                             std::vector<AudioDeviceError> errors)
    : devices_(std::move(devices)), errors_(std::move(errors)) {}

std::vector<AudioDeviceError> validate_audio_devices(
    const std::vector<AudioDeviceDescriptor>& devices) {
  std::vector<AudioDeviceError> errors;
  std::unordered_set<std::string> ids;

  for (const auto& device : devices) {
    if (device.id.empty()) {
      errors.push_back({"empty_device_id", "Audio device IDs must not be empty."});
    } else if (!ids.insert(device.id).second) {
      errors.push_back({"duplicate_device_id", "Audio device IDs must be unique."});
    }

    if (device.label.empty()) {
      errors.push_back({"empty_device_label", "Audio device labels must not be empty."});
    }

    if (device.formats.empty()) {
      errors.push_back({"empty_device_formats", "Audio devices must expose at least one format."});
    }

    for (const auto& format : device.formats) {
      if (format.sample_rate == 0) {
        errors.push_back({"invalid_sample_rate", "Audio device sample rates must be non-zero."});
      }
      if (format.channels == 0) {
        errors.push_back({"invalid_channel_count", "Audio device channel counts must be non-zero."});
      }
      if (format.frames_per_block == 0) {
        errors.push_back({"invalid_frames_per_block", "Audio device block sizes must be non-zero."});
      }
      if (format.bits_per_sample == 0) {
        errors.push_back({"invalid_bits_per_sample",
                          "Audio device bit depths must be non-zero."});
      }
      if (format.sample_format == AudioSampleFormat::Unknown) {
        errors.push_back({"invalid_sample_format",
                          "Audio device sample formats must be known."});
      }
      if (format.valid_bits_per_sample > format.bits_per_sample) {
        errors.push_back({"invalid_valid_bits_per_sample",
                          "Audio device valid bit depth cannot exceed container bit depth."});
      }
    }
  }

  return errors;
}

}  // namespace sar::platform
