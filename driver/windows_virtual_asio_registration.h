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
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64);

[[nodiscard]] WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver(
    WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64);

}  // namespace sar::driver
