#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sar::platform {

inline constexpr std::size_t kWindowsVirtualAsioMaxObjectTokenBytes = 64;

struct WindowsVirtualAsioObjectNames {
  std::wstring mapping;
  std::wstring input_event;
  std::wstring output_event;
  std::wstring shutdown_event;
  std::wstring client_disconnect_event;

  bool operator==(const WindowsVirtualAsioObjectNames&) const noexcept = default;
};

struct WindowsVirtualAsioObjectNameError {
  std::string code;
  std::string message;
};

class WindowsVirtualAsioObjectNamesResult {
 public:
  static WindowsVirtualAsioObjectNamesResult success(
      WindowsVirtualAsioObjectNames names);
  static WindowsVirtualAsioObjectNamesResult failure(
      std::vector<WindowsVirtualAsioObjectNameError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const WindowsVirtualAsioObjectNames& names() const noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioObjectNameError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioObjectNamesResult(
      std::optional<WindowsVirtualAsioObjectNames> names,
      std::vector<WindowsVirtualAsioObjectNameError> errors);

  std::optional<WindowsVirtualAsioObjectNames> names_;
  std::vector<WindowsVirtualAsioObjectNameError> errors_;
};

// Tokens are identifiers, not Windows object names. Only lowercase ASCII
// letters, digits, '-' and '_' are accepted so callers cannot select another
// object namespace or introduce a path separator.
[[nodiscard]] WindowsVirtualAsioObjectNamesResult
make_windows_virtual_asio_object_names(std::string_view endpoint_token,
                                       std::string_view client_token,
                                       std::uint64_t connection_generation);

}  // namespace sar::platform
