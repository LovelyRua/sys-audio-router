#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

enum class AudioBackendKind {
  Wasapi,
  WasapiLoopback,
  Asio,
  VirtualWasapi,
  VirtualAsio,
  Mock,
};

enum class AudioDeviceDirection {
  Input,
  Output,
  Duplex,
};

enum class AudioSampleFormat {
  Unknown,
  PcmInt,
  IeeeFloat,
};

struct AudioFormat {
  std::uint32_t sample_rate = 48000;
  std::uint32_t channels = 2;
  std::uint32_t frames_per_block = 128;
  std::uint32_t bits_per_sample = 32;
  std::uint32_t valid_bits_per_sample = 0;
  AudioSampleFormat sample_format = AudioSampleFormat::IeeeFloat;
};

struct AudioDeviceDescriptor {
  std::string id;
  std::string label;
  AudioBackendKind backend = AudioBackendKind::Mock;
  AudioDeviceDirection direction = AudioDeviceDirection::Duplex;
  std::vector<AudioFormat> formats;
  bool is_default = false;
  bool is_virtual = false;
};

struct AudioDeviceError {
  std::string code;
  std::string message;
};

class AudioDeviceListResult {
 public:
  static AudioDeviceListResult success(std::vector<AudioDeviceDescriptor> devices);
  static AudioDeviceListResult failure(std::vector<AudioDeviceError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<AudioDeviceDescriptor>& devices() const noexcept;
  [[nodiscard]] const std::vector<AudioDeviceError>& errors() const noexcept;

 private:
  AudioDeviceListResult(std::vector<AudioDeviceDescriptor> devices,
                        std::vector<AudioDeviceError> errors);

  std::vector<AudioDeviceDescriptor> devices_;
  std::vector<AudioDeviceError> errors_;
};

class AudioDeviceProvider {
 public:
  virtual ~AudioDeviceProvider() = default;

  [[nodiscard]] virtual AudioBackendKind backend() const noexcept = 0;
  [[nodiscard]] virtual AudioDeviceListResult list_devices() const = 0;
};

[[nodiscard]] std::vector<AudioDeviceError> validate_audio_devices(
    const std::vector<AudioDeviceDescriptor>& devices);

}  // namespace sar::platform
