#include "core/platform/windows_virtual_asio_object_names.h"

#include <array>
#include <utility>

namespace sar::platform {

namespace {

constexpr std::wstring_view kObjectNamePrefix =
    L"Local\\SAR.VirtualASIO.v1.endpoint.";

bool is_object_token_character(unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('_');
}

void validate_token(std::string_view token,
                    std::string_view token_kind,
                    std::vector<WindowsVirtualAsioObjectNameError>& errors) {
  if (token.empty()) {
    errors.push_back({
        "empty_virtual_asio_" + std::string(token_kind) + "_token",
        "Virtual ASIO object tokens must not be empty.",
    });
    return;
  }
  if (token.size() > kWindowsVirtualAsioMaxObjectTokenBytes) {
    errors.push_back({
        "virtual_asio_" + std::string(token_kind) + "_token_too_long",
        "Virtual ASIO object token exceeds the bounded naming limit.",
    });
    return;
  }
  for (const unsigned char value : token) {
    if (!is_object_token_character(value)) {
      errors.push_back({
          "invalid_virtual_asio_" + std::string(token_kind) + "_token",
          "Virtual ASIO object tokens require lowercase ASCII letters, digits, '-' or '_'.",
      });
      return;
    }
  }
}

std::wstring widen_ascii(std::string_view value) {
  std::wstring result;
  result.reserve(value.size());
  for (const unsigned char character : value) {
    result.push_back(static_cast<wchar_t>(character));
  }
  return result;
}

std::wstring generation_hex(std::uint64_t generation) {
  constexpr std::array<wchar_t, 16> digits = {
      L'0', L'1', L'2', L'3', L'4', L'5', L'6', L'7',
      L'8', L'9', L'a', L'b', L'c', L'd', L'e', L'f',
  };
  std::wstring result(16, L'0');
  for (std::size_t index = result.size(); index > 0; --index) {
    result[index - 1] = digits[generation & 0x0fU];
    generation >>= 4U;
  }
  return result;
}

std::wstring make_base_name(std::string_view endpoint_token,
                            std::string_view client_token,
                            std::uint64_t connection_generation) {
  std::wstring result(kObjectNamePrefix);
  result += widen_ascii(endpoint_token);
  result += L".client.";
  result += widen_ascii(client_token);
  result += L".generation.";
  result += generation_hex(connection_generation);
  return result;
}

}  // namespace

WindowsVirtualAsioObjectNamesResult
WindowsVirtualAsioObjectNamesResult::success(
    WindowsVirtualAsioObjectNames names) {
  return {std::move(names), {}};
}

WindowsVirtualAsioObjectNamesResult
WindowsVirtualAsioObjectNamesResult::failure(
    std::vector<WindowsVirtualAsioObjectNameError> errors) {
  return {std::nullopt, std::move(errors)};
}

bool WindowsVirtualAsioObjectNamesResult::ok() const noexcept {
  return names_.has_value() && errors_.empty();
}

const WindowsVirtualAsioObjectNames&
WindowsVirtualAsioObjectNamesResult::names() const noexcept {
  return *names_;
}

const std::vector<WindowsVirtualAsioObjectNameError>&
WindowsVirtualAsioObjectNamesResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioObjectNamesResult::WindowsVirtualAsioObjectNamesResult(
    std::optional<WindowsVirtualAsioObjectNames> names,
    std::vector<WindowsVirtualAsioObjectNameError> errors)
    : names_(std::move(names)), errors_(std::move(errors)) {}

WindowsVirtualAsioObjectNamesResult make_windows_virtual_asio_object_names(
    std::string_view endpoint_token,
    std::string_view client_token,
    std::uint64_t connection_generation) {
  std::vector<WindowsVirtualAsioObjectNameError> errors;
  validate_token(endpoint_token, "endpoint", errors);
  validate_token(client_token, "client", errors);
  if (connection_generation == 0) {
    errors.push_back({
        "invalid_virtual_asio_connection_generation",
        "Virtual ASIO connection generation must be non-zero.",
    });
  }
  if (!errors.empty()) {
    return WindowsVirtualAsioObjectNamesResult::failure(std::move(errors));
  }

  const auto base =
      make_base_name(endpoint_token, client_token, connection_generation);
  return WindowsVirtualAsioObjectNamesResult::success({
      .mapping = base + L".mapping",
      .input_event = base + L".input-event",
      .output_event = base + L".output-event",
      .shutdown_event = base + L".shutdown-event",
  });
}

}  // namespace sar::platform
