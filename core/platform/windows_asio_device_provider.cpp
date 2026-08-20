#include "core/platform/windows_asio_device_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_set>
#include <utility>

namespace sar::platform {

namespace {

class RegistryKey {
 public:
  RegistryKey() = default;
  RegistryKey(const RegistryKey&) = delete;
  RegistryKey& operator=(const RegistryKey&) = delete;
  ~RegistryKey() {
    if (key_ != nullptr) {
      RegCloseKey(key_);
    }
  }

  [[nodiscard]] HKEY* put() noexcept { return &key_; }
  [[nodiscard]] HKEY get() const noexcept { return key_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return key_ != nullptr;
  }

 private:
  HKEY key_ = nullptr;
};

std::string win32_error(LSTATUS status) {
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "0x%08lX",
                static_cast<unsigned long>(status));
  return buffer;
}

std::string wide_to_utf8(const std::wstring& value) {
  if (value.empty()) {
    return {};
  }
  const auto size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                        value.data(),
                                        static_cast<int>(value.size()),
                                        nullptr, 0, nullptr, nullptr);
  if (size <= 0) {
    return {};
  }
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

bool read_string(HKEY key, const wchar_t* name, std::wstring& value) {
  DWORD type = 0;
  DWORD bytes = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) !=
          ERROR_SUCCESS ||
      (type != REG_SZ && type != REG_EXPAND_SZ) ||
      bytes < sizeof(wchar_t)) {
    return false;
  }
  std::wstring buffer(bytes / sizeof(wchar_t), L'\0');
  if (RegQueryValueExW(key, name, nullptr, &type,
                       reinterpret_cast<BYTE*>(buffer.data()), &bytes) !=
      ERROR_SUCCESS) {
    return false;
  }
  while (!buffer.empty() && buffer.back() == L'\0') {
    buffer.pop_back();
  }
  if (type == REG_EXPAND_SZ && !buffer.empty()) {
    const auto required = ExpandEnvironmentStringsW(buffer.c_str(), nullptr, 0);
    if (required > 1) {
      std::wstring expanded(required, L'\0');
      if (ExpandEnvironmentStringsW(buffer.c_str(), expanded.data(),
                                    required) == required) {
        while (!expanded.empty() && expanded.back() == L'\0') {
          expanded.pop_back();
        }
        buffer = std::move(expanded);
      }
    }
  }
  value = std::move(buffer);
  return true;
}

std::wstring inproc_path(const std::wstring& clsid) {
  return L"SOFTWARE\\Classes\\CLSID\\" + clsid + L"\\InprocServer32";
}

std::string read_dll_path(HKEY root, const std::wstring& clsid) {
  RegistryKey key;
  if (RegOpenKeyExW(root, inproc_path(clsid).c_str(), 0,
                    KEY_READ | KEY_WOW64_64KEY, key.put()) != ERROR_SUCCESS) {
    return {};
  }
  std::wstring value;
  return read_string(key.get(), nullptr, value) ? wide_to_utf8(value)
                                                : std::string{};
}

WindowsAsioRegistryResult enumerate_root(HKEY root, bool current_user) {
  RegistryKey asio;
  const auto open = RegOpenKeyExW(root, L"SOFTWARE\\ASIO", 0,
                                  KEY_READ | KEY_WOW64_64KEY, asio.put());
  if (open == ERROR_FILE_NOT_FOUND) {
    return WindowsAsioRegistryResult::success({});
  }
  if (open != ERROR_SUCCESS) {
    return WindowsAsioRegistryResult::failure({
        "asio_registry_open_failed",
        "Opening the x64 ASIO registry failed with " + win32_error(open) +
            ".",
    });
  }

  std::vector<WindowsAsioRegistryEntry> entries;
  DWORD index = 0;
  for (;;) {
    std::wstring name(256, L'\0');
    DWORD name_size = static_cast<DWORD>(name.size());
    FILETIME written{};
    const auto status = RegEnumKeyExW(asio.get(), index, name.data(),
                                      &name_size, nullptr, nullptr, nullptr,
                                      &written);
    if (status == ERROR_NO_MORE_ITEMS) {
      break;
    }
    if (status == ERROR_MORE_DATA) {
      name.resize(32768);
      name_size = static_cast<DWORD>(name.size());
      const auto retry = RegEnumKeyExW(asio.get(), index, name.data(),
                                       &name_size, nullptr, nullptr, nullptr,
                                       &written);
      if (retry != ERROR_SUCCESS) {
        ++index;
        continue;
      }
    } else if (status != ERROR_SUCCESS) {
      return WindowsAsioRegistryResult::failure({
          "asio_registry_enumeration_failed",
          "Enumerating x64 ASIO drivers failed with " +
              win32_error(status) + ".",
      });
    }
    name.resize(name_size);
    ++index;

    RegistryKey driver;
    if (RegOpenKeyExW(asio.get(), name.c_str(), 0,
                      KEY_READ | KEY_WOW64_64KEY, driver.put()) !=
        ERROR_SUCCESS) {
      continue;
    }
    std::wstring clsid;
    if (!read_string(driver.get(), L"CLSID", clsid) || clsid.empty()) {
      continue;
    }
    std::wstring description;
    static_cast<void>(read_string(driver.get(), L"Description", description));
    entries.push_back({
        .registry_name = wide_to_utf8(name),
        .clsid = wide_to_utf8(clsid),
        .description = wide_to_utf8(description),
        .dll_path = read_dll_path(root, clsid),
        .current_user = current_user,
    });
  }
  return WindowsAsioRegistryResult::success(std::move(entries));
}

std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

}  // namespace

WindowsAsioRegistryResult WindowsAsioRegistryResult::success(
    std::vector<WindowsAsioRegistryEntry> entries) {
  return {std::move(entries), {}, true};
}

WindowsAsioRegistryResult WindowsAsioRegistryResult::failure(
    AudioDeviceError error) {
  return {{}, std::move(error), false};
}

bool WindowsAsioRegistryResult::ok() const noexcept { return succeeded_; }

const std::vector<WindowsAsioRegistryEntry>& WindowsAsioRegistryResult::entries()
    const noexcept {
  return entries_;
}

const AudioDeviceError& WindowsAsioRegistryResult::error() const noexcept {
  return error_;
}

WindowsAsioRegistryResult::WindowsAsioRegistryResult(
    std::vector<WindowsAsioRegistryEntry> entries,
    AudioDeviceError error,
    bool succeeded) noexcept
    : entries_(std::move(entries)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

WindowsAsioRegistryResult enumerate_windows_asio_registry() {
  auto user = enumerate_root(HKEY_CURRENT_USER, true);
  if (!user.ok()) {
    return user;
  }
  auto machine = enumerate_root(HKEY_LOCAL_MACHINE, false);
  if (!machine.ok()) {
    return machine;
  }

  auto entries = user.entries();
  std::unordered_set<std::string> identities;
  for (const auto& entry : entries) {
    identities.insert(lowercase(entry.clsid));
  }
  for (const auto& entry : machine.entries()) {
    if (identities.insert(lowercase(entry.clsid)).second) {
      entries.push_back(entry);
    }
  }
  return WindowsAsioRegistryResult::success(std::move(entries));
}

WindowsAsioDeviceProvider::WindowsAsioDeviceProvider()
    : WindowsAsioDeviceProvider(enumerate_windows_asio_registry,
                                probe_windows_asio_driver) {}

WindowsAsioDeviceProvider::WindowsAsioDeviceProvider(
    WindowsAsioRegistryReader registry_reader,
    WindowsAsioProbeFunction probe)
    : registry_reader_(std::move(registry_reader)), probe_(std::move(probe)) {}

AudioBackendKind WindowsAsioDeviceProvider::backend() const noexcept {
  return AudioBackendKind::Asio;
}

AudioDeviceListResult WindowsAsioDeviceProvider::list_devices() const {
  if (!registry_reader_ || !probe_) {
    return AudioDeviceListResult::failure({{
        "asio_provider_not_configured",
        "ASIO device provider dependencies are not configured.",
    }});
  }
  const auto registry = registry_reader_();
  if (!registry.ok()) {
    return AudioDeviceListResult::failure({registry.error()});
  }

  std::vector<AudioDeviceDescriptor> devices;
  AudioDeviceError first_probe_error;
  for (const auto& entry : registry.entries()) {
    const auto probed = probe_(entry.clsid);
    if (!probed.ok()) {
      if (first_probe_error.code.empty()) {
        first_probe_error = probed.error();
      }
      continue;
    }
    const auto& probe = probed.probe();
    AudioDeviceDescriptor device;
    device.id = "asio:" + lowercase(entry.clsid);
    device.label = !entry.description.empty()
                       ? entry.description
                       : (!probe.driver_name.empty() ? probe.driver_name
                                                     : entry.registry_name);
    device.backend = AudioBackendKind::Asio;
    device.direction = probe.input_channels > 0 && probe.output_channels > 0
                           ? AudioDeviceDirection::Duplex
                       : probe.input_channels > 0
                           ? AudioDeviceDirection::Input
                           : AudioDeviceDirection::Output;
    const auto channels =
        std::max(probe.input_channels, probe.output_channels);
    for (const auto rate : probe.supported_sample_rates) {
      device.formats.push_back({
          .sample_rate = rate,
          .channels = channels,
          .frames_per_block = probe.preferred_buffer_frames,
          .bits_per_sample = probe.bits_per_sample,
          .valid_bits_per_sample = probe.bits_per_sample,
          .sample_format = probe.sample_format,
      });
    }
    devices.push_back(std::move(device));
  }
  if (devices.empty() && !registry.entries().empty() &&
      !first_probe_error.code.empty()) {
    return AudioDeviceListResult::failure({std::move(first_probe_error)});
  }
  auto errors = validate_audio_devices(devices);
  if (!errors.empty()) {
    return AudioDeviceListResult::failure(std::move(errors));
  }
  return AudioDeviceListResult::success(std::move(devices));
}

}  // namespace sar::platform
