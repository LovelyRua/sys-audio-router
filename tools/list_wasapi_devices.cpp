#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <iostream>

namespace {

const char* direction_name(sar::platform::AudioDeviceDirection direction) {
  switch (direction) {
    case sar::platform::AudioDeviceDirection::Input:
      return "input";
    case sar::platform::AudioDeviceDirection::Output:
      return "output";
    case sar::platform::AudioDeviceDirection::Duplex:
      return "duplex";
  }
  return "unknown";
}

const char* sample_format_name(sar::platform::AudioSampleFormat format) {
  switch (format) {
    case sar::platform::AudioSampleFormat::Unknown:
      return "unknown";
    case sar::platform::AudioSampleFormat::PcmInt:
      return "pcm-int";
    case sar::platform::AudioSampleFormat::IeeeFloat:
      return "ieee-float";
  }
  return "unknown";
}

void print_probe() {
  const auto result = sar::platform::probe_default_wasapi_stream(
      sar::platform::WasapiStreamDirection::Render);
  if (!result.ok()) {
    std::cout << "\nDefault render stream probe: unavailable\n";
    for (const auto& error : result.errors()) {
      std::cout << "  " << error.code << ": " << error.message << '\n';
    }
    return;
  }

  const auto& probe = result.probe();
  std::cout << "\nDefault render stream probe\n";
  std::cout << "  Device: " << probe.device_label << '\n';
  std::cout << "  Sample rate: " << probe.mix_format.sample_rate << '\n';
  std::cout << "  Channels: " << probe.mix_format.channels << '\n';
  std::cout << "  Sample format: " << sample_format_name(probe.mix_format.sample_format)
            << ", " << probe.mix_format.bits_per_sample << " bit\n";
  std::cout << "  Buffer frames: " << probe.buffer_frames << '\n';
  std::cout << "  Default period 100ns: " << probe.default_period_100ns << '\n';
  std::cout << "  Minimum period 100ns: " << probe.minimum_period_100ns << '\n';
}

}  // namespace

int main() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::cout << "Active WASAPI endpoints: " << result.devices().size() << '\n';
  for (const auto& device : result.devices()) {
    std::cout << "- " << device.label << '\n';
    std::cout << "  ID: " << device.id << '\n';
    std::cout << "  Direction: " << direction_name(device.direction) << '\n';
    std::cout << "  Default: " << (device.is_default ? "yes" : "no") << '\n';
    for (const auto& format : device.formats) {
      std::cout << "  Format: " << format.sample_rate << " Hz, "
                << format.channels << " channels, "
                << sample_format_name(format.sample_format) << ", "
                << format.bits_per_sample << " bit, "
                << format.frames_per_block << " frames\n";
    }
  }

  print_probe();
  return 0;
}
