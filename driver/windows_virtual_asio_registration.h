#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sar::driver {

enum class WindowsVirtualAsioRegistryView {
  X64,
  X86,
  ProcessDefault,
};

enum class WindowsVirtualAsioRegistrationScope {
  CurrentUser,
  LocalMachine,
};

struct WindowsVirtualAsioInstanceDescriptor {
  // Braced GUID text, for example {7F16C8A9-...}. Registration canonicalizes
  // this value before using it as a registry path or discovery value.
  std::wstring clsid;
  std::wstring display_name;
  std::wstring registry_name;
  std::wstring broker_token;
};

struct WindowsVirtualAsioRegistrationError {
  std::string code;
  std::string message;
  std::uint32_t native_win32_code = 0;
};

class WindowsVirtualAsioRegistrationResult {
 public:
  static WindowsVirtualAsioRegistrationResult success();
  static WindowsVirtualAsioRegistrationResult failure(
      std::vector<WindowsVirtualAsioRegistrationError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioRegistrationError>& errors()
      const noexcept;

 private:
  explicit WindowsVirtualAsioRegistrationResult(
      std::vector<WindowsVirtualAsioRegistrationError> errors);

  std::vector<WindowsVirtualAsioRegistrationError> errors_;
};

[[nodiscard]] WindowsVirtualAsioRegistrationResult
register_windows_virtual_asio_driver(
    std::wstring dll_path,
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
register_windows_virtual_asio_driver(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
verify_windows_virtual_asio_driver_registration(
    std::wstring dll_path,
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
verify_windows_virtual_asio_driver_registration(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver(
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver(
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver_if_owned(
    std::wstring dll_path,
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver_if_owned(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64,
    WindowsVirtualAsioRegistrationScope scope =
        WindowsVirtualAsioRegistrationScope::LocalMachine);

}  // namespace sar::driver
