#include "core/platform/windows_asio_device_provider.h"

#include <iostream>

int main() {
  const auto registry = sar::platform::enumerate_windows_asio_registry();
  if (!registry.ok()) {
    std::cerr << registry.error().code << ": " << registry.error().message
              << '\n';
    return 1;
  }

  std::cout << "asio_registry_devices=" << registry.entries().size() << '\n';
  bool probe_failed = false;
  for (const auto& entry : registry.entries()) {
    std::cout << "name=\"" << entry.registry_name << "\" clsid=\""
              << entry.clsid << "\" scope="
              << (entry.current_user ? "user" : "machine") << " dll=\""
              << entry.dll_path << "\"\n";
    const auto probed =
        sar::platform::probe_windows_asio_driver(entry.clsid);
    if (!probed.ok()) {
      probe_failed = true;
      std::cout << "  probe=failed code=" << probed.error().code
                << " message=\"" << probed.error().message << "\"\n";
      continue;
    }
    const auto& probe = probed.probe();
    std::cout << "  probe=ok driver=\"" << probe.driver_name
              << "\" version=" << probe.driver_version
              << " inputs=" << probe.input_channels
              << " outputs=" << probe.output_channels
              << " buffer_min=" << probe.minimum_buffer_frames
              << " buffer_max=" << probe.maximum_buffer_frames
              << " buffer_preferred=" << probe.preferred_buffer_frames
              << " sample_rates=";
    for (std::size_t index = 0; index < probe.supported_sample_rates.size();
         ++index) {
      if (index > 0) {
        std::cout << ',';
      }
      std::cout << probe.supported_sample_rates[index];
    }
    std::cout << '\n';
  }
  return probe_failed ? 2 : 0;
}
