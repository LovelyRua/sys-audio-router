#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

struct WindowsVirtualAsioSecurityError {
  std::string code;
  std::string message;
  std::uint32_t native_error = 0;
};

class WindowsVirtualAsioSecurityAttributes;

class WindowsVirtualAsioSecurityResult {
 public:
  static WindowsVirtualAsioSecurityResult success(
      std::unique_ptr<WindowsVirtualAsioSecurityAttributes> attributes);
  static WindowsVirtualAsioSecurityResult failure(
      std::vector<WindowsVirtualAsioSecurityError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioSecurityAttributes& attributes() noexcept;
  [[nodiscard]] const WindowsVirtualAsioSecurityAttributes& attributes()
      const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsVirtualAsioSecurityAttributes>
  take_attributes() noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioSecurityError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioSecurityResult(
      std::unique_ptr<WindowsVirtualAsioSecurityAttributes> attributes,
      std::vector<WindowsVirtualAsioSecurityError> errors);

  std::unique_ptr<WindowsVirtualAsioSecurityAttributes> attributes_;
  std::vector<WindowsVirtualAsioSecurityError> errors_;
};

class WindowsVirtualAsioSecurityAttributes {
 public:
  WindowsVirtualAsioSecurityAttributes(
      const WindowsVirtualAsioSecurityAttributes&) = delete;
  WindowsVirtualAsioSecurityAttributes& operator=(
      const WindowsVirtualAsioSecurityAttributes&) = delete;
  WindowsVirtualAsioSecurityAttributes(
      WindowsVirtualAsioSecurityAttributes&& other) noexcept;
  WindowsVirtualAsioSecurityAttributes& operator=(
      WindowsVirtualAsioSecurityAttributes&& other) noexcept;
  ~WindowsVirtualAsioSecurityAttributes();

  [[nodiscard]] static WindowsVirtualAsioSecurityResult create_for_current_user();

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] void* native_attributes() noexcept;
  [[nodiscard]] const void* native_attributes() const noexcept;

 private:
  struct Impl;

  explicit WindowsVirtualAsioSecurityAttributes(
      std::unique_ptr<Impl> impl) noexcept;

  std::unique_ptr<Impl> impl_;
};

}  // namespace sar::platform
