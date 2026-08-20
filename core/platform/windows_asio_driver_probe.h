#pragma once

#include "core/platform/audio_device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

struct WindowsAsioDriverProbe {
  std::string clsid;
  std::string driver_name;
  long driver_version = 0;
  std::uint32_t input_channels = 0;
  std::uint32_t output_channels = 0;
  std::uint32_t minimum_buffer_frames = 0;
  std::uint32_t maximum_buffer_frames = 0;
  std::uint32_t preferred_buffer_frames = 0;
  long buffer_granularity = 0;
  double current_sample_rate = 0.0;
  AudioSampleFormat sample_format = AudioSampleFormat::Unknown;
  std::uint32_t bits_per_sample = 0;
  std::vector<std::uint32_t> supported_sample_rates;
};

class WindowsAsioDriverProbeResult {
 public:
  static WindowsAsioDriverProbeResult success(WindowsAsioDriverProbe probe);
  static WindowsAsioDriverProbeResult failure(AudioDeviceError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const WindowsAsioDriverProbe& probe() const noexcept;
  [[nodiscard]] const AudioDeviceError& error() const noexcept;

 private:
  WindowsAsioDriverProbeResult(WindowsAsioDriverProbe probe,
                               AudioDeviceError error,
                               bool succeeded) noexcept;

  WindowsAsioDriverProbe probe_;
  AudioDeviceError error_;
  bool succeeded_ = false;
};

[[nodiscard]] WindowsAsioDriverProbeResult probe_windows_asio_driver(
    const std::string& clsid);

}  // namespace sar::platform
