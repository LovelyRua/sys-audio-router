#pragma once

#include "core/platform/audio_device.h"
#include "core/platform/windows_asio_driver_probe.h"

#include <functional>
#include <string>
#include <vector>

namespace sar::platform {

struct WindowsAsioRegistryEntry {
  std::string registry_name;
  std::string clsid;
  std::string description;
  std::string dll_path;
  bool current_user = false;
};

class WindowsAsioRegistryResult {
 public:
  static WindowsAsioRegistryResult success(
      std::vector<WindowsAsioRegistryEntry> entries);
  static WindowsAsioRegistryResult failure(AudioDeviceError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WindowsAsioRegistryEntry>& entries()
      const noexcept;
  [[nodiscard]] const AudioDeviceError& error() const noexcept;

 private:
  WindowsAsioRegistryResult(std::vector<WindowsAsioRegistryEntry> entries,
                            AudioDeviceError error,
                            bool succeeded) noexcept;

  std::vector<WindowsAsioRegistryEntry> entries_;
  AudioDeviceError error_;
  bool succeeded_ = false;
};

using WindowsAsioRegistryReader =
    std::function<WindowsAsioRegistryResult()>;
using WindowsAsioProbeFunction =
    std::function<WindowsAsioDriverProbeResult(const std::string&)>;

[[nodiscard]] WindowsAsioRegistryResult enumerate_windows_asio_registry();

class WindowsAsioDeviceProvider final : public AudioDeviceProvider {
 public:
  WindowsAsioDeviceProvider();
  WindowsAsioDeviceProvider(WindowsAsioRegistryReader registry_reader,
                            WindowsAsioProbeFunction probe);

  [[nodiscard]] AudioBackendKind backend() const noexcept override;
  [[nodiscard]] AudioDeviceListResult list_devices() const override;

 private:
  WindowsAsioRegistryReader registry_reader_;
  WindowsAsioProbeFunction probe_;
};

}  // namespace sar::platform
