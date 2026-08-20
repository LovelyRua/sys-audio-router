#include "core/platform/windows_asio_device_provider.h"

#include <cassert>
#include <string>
#include <utility>

namespace {

sar::platform::WindowsAsioDriverProbeResult probe(const std::string& clsid) {
  if (clsid == "{BROKEN}") {
    return sar::platform::WindowsAsioDriverProbeResult::failure(
        {"injected_probe_failure", "Injected ASIO probe failure."});
  }
  sar::platform::WindowsAsioDriverProbe result;
  result.clsid = clsid;
  result.driver_name = "Injected ASIO";
  result.driver_version = 42;
  result.input_channels = 8;
  result.output_channels = 4;
  result.minimum_buffer_frames = 32;
  result.maximum_buffer_frames = 1024;
  result.preferred_buffer_frames = 128;
  result.buffer_granularity = -1;
  result.current_sample_rate = 48000.0;
  result.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  result.bits_per_sample = 32;
  result.supported_sample_rates = {44100, 48000, 96000};
  return sar::platform::WindowsAsioDriverProbeResult::success(
      std::move(result));
}

}  // namespace

int main() {
  using namespace sar::platform;

  std::uint32_t probe_calls = 0;

  WindowsAsioDeviceProvider provider(
      [] {
        return WindowsAsioRegistryResult::success({
            {
                .registry_name = "Injected ASIO",
                .clsid = "{11111111-2222-3333-4444-555555555555}",
                .description = "Studio Hardware ASIO",
                .dll_path = "C:\\Injected\\asio.dll",
                .current_user = false,
            },
            {
                .registry_name = "Broken ASIO",
                .clsid = "{BROKEN}",
                .description = "Broken ASIO",
                .dll_path = "C:\\Injected\\broken.dll",
                .current_user = true,
            },
        });
      },
      [&](const std::string& clsid) {
        ++probe_calls;
        return probe(clsid);
      });
  assert(provider.backend() == AudioBackendKind::Asio);
  const auto listed = provider.list_devices();
  assert(listed.ok());
  assert(listed.devices().size() == 2);
  assert(probe_calls == 0);
  const auto& device = listed.devices().front();
  assert(device.id ==
         "asio:{11111111-2222-3333-4444-555555555555}");
  assert(device.label == "Studio Hardware ASIO");
  assert(device.backend == AudioBackendKind::Asio);
  assert(device.direction == AudioDeviceDirection::Duplex);
  assert(device.input_channels == 0);
  assert(device.output_channels == 0);
  assert(device.formats.size() == 1);
  assert(device.formats[0].sample_rate == 48000);
  assert(device.formats[0].channels == 1);
  assert(device.formats[0].frames_per_block == 128);
  assert(device.formats[0].sample_format == AudioSampleFormat::IeeeFloat);

  WindowsAsioDeviceProvider all_broken(
      [] {
        return WindowsAsioRegistryResult::success({{
            .registry_name = "Broken ASIO",
            .clsid = "{BROKEN}",
        }});
      },
      probe);
  const auto failed = all_broken.list_devices();
  assert(failed.ok());
  assert(failed.devices().size() == 1);

  WindowsAsioDeviceProvider empty(
      [] { return WindowsAsioRegistryResult::success({}); }, probe);
  const auto empty_result = empty.list_devices();
  assert(empty_result.ok());
  assert(empty_result.devices().empty());

  WindowsAsioDeviceProvider registry_failure(
      [] {
        return WindowsAsioRegistryResult::failure(
            {"injected_registry_failure", "Injected registry failure."});
      },
      probe);
  const auto failed_registry = registry_failure.list_devices();
  assert(!failed_registry.ok());
  assert(failed_registry.errors().front().code ==
         "injected_registry_failure");
}
