#include "driver/windows_virtual_asio_registration.h"

#include "driver/windows_virtual_asio_com.h"

#include <Windows.h>

#include <cwchar>
#include <string_view>
#include <utility>
#include <vector>

namespace sar::driver {
namespace {

constexpr wchar_t kAsioKey[] =
    L"Software\\ASIO\\System Audio Route";
constexpr wchar_t kClsidKey[] =
    L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}";
constexpr wchar_t kInprocKey[] =
    L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\\InprocServer32";

HKEY registry_root(WindowsVirtualAsioRegistrationScope scope) noexcept {
  return scope == WindowsVirtualAsioRegistrationScope::CurrentUser
             ? HKEY_CURRENT_USER
             : HKEY_LOCAL_MACHINE;
}

REGSAM view_access(WindowsVirtualAsioRegistryView view) noexcept {
  switch (view) {
    case WindowsVirtualAsioRegistryView::X64:
      return KEY_WOW64_64KEY;
    case WindowsVirtualAsioRegistryView::X86:
      return KEY_WOW64_32KEY;
    case WindowsVirtualAsioRegistryView::ProcessDefault:
      return 0;
  }
  return 0;
}

WindowsVirtualAsioRegistrationResult native_failure(
    std::string code, std::string message, LSTATUS native) {
  return WindowsVirtualAsioRegistrationResult::failure({{
      std::move(code), std::move(message), static_cast<std::uint32_t>(native),
  }});
}

LSTATUS set_string(HKEY key, const wchar_t* name,
                   const std::wstring& value) noexcept {
  return RegSetValueExW(
      key, name, 0, REG_SZ,
      reinterpret_cast<const BYTE*>(value.c_str()),
      static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

LSTATUS create_key(HKEY root,
                   const wchar_t* path,
                   REGSAM access,
                   HKEY& key) noexcept {
  return RegCreateKeyExW(root, path, 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE | access, nullptr,
                         &key, nullptr);
}

LSTATUS delete_child(HKEY root,
                     const wchar_t* parent,
                     const wchar_t* child,
                     REGSAM access) noexcept {
  HKEY key = nullptr;
  const auto opened = RegOpenKeyExW(root, parent, 0,
                                    KEY_WRITE | access, &key);
  if (opened == ERROR_FILE_NOT_FOUND || opened == ERROR_PATH_NOT_FOUND) {
    return ERROR_SUCCESS;
  }
  if (opened != ERROR_SUCCESS) {
    return opened;
  }
  const auto deleted = RegDeleteTreeW(key, child);
  RegCloseKey(key);
  return deleted == ERROR_FILE_NOT_FOUND || deleted == ERROR_PATH_NOT_FOUND
             ? ERROR_SUCCESS
             : deleted;
}

WindowsVirtualAsioRegistrationResult resolve_driver_path(
    const std::wstring& dll_path, std::wstring& absolute) {
  if (dll_path.empty()) {
    return native_failure("virtual_asio_dll_path_empty",
                          "Virtual ASIO DLL path must not be empty.",
                          ERROR_INVALID_PARAMETER);
  }
  const DWORD required = GetFullPathNameW(dll_path.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return native_failure("virtual_asio_dll_path_invalid",
                          "Could not resolve the Virtual ASIO DLL path.",
                          GetLastError());
  }
  absolute.assign(required, L'\0');
  const DWORD written = GetFullPathNameW(
      dll_path.c_str(), required, absolute.data(), nullptr);
  if (written == 0 || written >= required) {
    return native_failure("virtual_asio_dll_path_invalid",
                          "Could not resolve the Virtual ASIO DLL path.",
                          GetLastError());
  }
  absolute.resize(written);
  const DWORD attributes = GetFileAttributesW(absolute.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES ||
      (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return native_failure("virtual_asio_dll_not_found",
                          "Virtual ASIO DLL does not exist.",
                          attributes == INVALID_FILE_ATTRIBUTES
                              ? GetLastError()
                              : ERROR_DIRECTORY);
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

LSTATUS read_string(HKEY root,
                    const wchar_t* path,
                    const wchar_t* name,
                    REGSAM access,
                    std::wstring& value) {
  HKEY key = nullptr;
  auto native = RegOpenKeyExW(root, path, 0, KEY_QUERY_VALUE | access, &key);
  if (native != ERROR_SUCCESS) {
    return native;
  }
  DWORD bytes = 0;
  DWORD type = 0;
  native = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
  if (native != ERROR_SUCCESS) {
    RegCloseKey(key);
    return native;
  }
  if (type != REG_SZ || bytes < sizeof(wchar_t) ||
      bytes % sizeof(wchar_t) != 0) {
    RegCloseKey(key);
    return ERROR_INVALID_DATA;
  }
  std::vector<wchar_t> buffer(bytes / sizeof(wchar_t));
  native = RegQueryValueExW(key, name, nullptr, &type,
                            reinterpret_cast<BYTE*>(buffer.data()), &bytes);
  RegCloseKey(key);
  if (native == ERROR_SUCCESS) {
    buffer.back() = L'\0';
    value.assign(buffer.data());
  }
  return native;
}

bool equal_path(std::wstring_view left, std::wstring_view right) noexcept {
  return CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                              right.data(), static_cast<int>(right.size()),
                              TRUE) == CSTR_EQUAL;
}

void append_value_error(std::vector<WindowsVirtualAsioRegistrationError>& errors,
                        HKEY root,
                        const wchar_t* path,
                        const wchar_t* name,
                        const wchar_t* expected,
                        const char* code,
                        const char* message,
                        REGSAM access,
                        bool path_value = false) {
  std::wstring actual;
  const auto native = read_string(root, path, name, access, actual);
  if (native != ERROR_SUCCESS ||
      (path_value ? !equal_path(actual, expected) : actual != expected)) {
    errors.push_back({code, message, static_cast<std::uint32_t>(
        native == ERROR_SUCCESS ? ERROR_INVALID_DATA : native)});
  }
}

}  // namespace

WindowsVirtualAsioRegistrationResult
WindowsVirtualAsioRegistrationResult::success() {
  return WindowsVirtualAsioRegistrationResult({});
}

WindowsVirtualAsioRegistrationResult
WindowsVirtualAsioRegistrationResult::failure(
    std::vector<WindowsVirtualAsioRegistrationError> errors) {
  return WindowsVirtualAsioRegistrationResult(std::move(errors));
}

WindowsVirtualAsioRegistrationResult::WindowsVirtualAsioRegistrationResult(
    std::vector<WindowsVirtualAsioRegistrationError> errors)
    : errors_(std::move(errors)) {}

bool WindowsVirtualAsioRegistrationResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<WindowsVirtualAsioRegistrationError>&
WindowsVirtualAsioRegistrationResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioRegistrationResult register_windows_virtual_asio_driver(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  std::wstring absolute;
  const auto resolved = resolve_driver_path(dll_path, absolute);
  if (!resolved.ok()) {
    return resolved;
  }

  const auto access = view_access(view);
  const auto root = registry_root(scope);
  HKEY clsid = nullptr;
  auto native = create_key(root, kClsidKey, access, clsid);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_key_create_failed",
                          "Could not create the COM class key.", native);
  }
  native = set_string(clsid, nullptr, kWindowsVirtualAsioDisplayName);
  RegCloseKey(clsid);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_name_write_failed",
                          "Could not write the COM class name.", native);
  }

  HKEY inproc = nullptr;
  native = create_key(root, kInprocKey, access, inproc);
  if (native == ERROR_SUCCESS) {
    native = set_string(inproc, nullptr, absolute);
  }
  if (native == ERROR_SUCCESS) {
    native = set_string(inproc, L"ThreadingModel", L"Both");
  }
  if (inproc != nullptr) {
    RegCloseKey(inproc);
  }
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_inproc_write_failed",
                          "Could not write the COM in-process server.", native);
  }

  HKEY asio = nullptr;
  native = create_key(root, kAsioKey, access, asio);
  if (native == ERROR_SUCCESS) {
    native = set_string(asio, L"CLSID", kWindowsVirtualAsioClsidString);
  }
  if (native == ERROR_SUCCESS) {
    native = set_string(asio, L"Description", kWindowsVirtualAsioDisplayName);
  }
  if (asio != nullptr) {
    RegCloseKey(asio);
  }
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_discovery_write_failed",
                          "Could not write the ASIO discovery key.", native);
  }
  return verify_windows_virtual_asio_driver_registration(
      std::move(absolute), view, scope);
}

WindowsVirtualAsioRegistrationResult
verify_windows_virtual_asio_driver_registration(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  std::wstring absolute;
  const auto resolved = resolve_driver_path(dll_path, absolute);
  if (!resolved.ok()) {
    return resolved;
  }

  const auto access = view_access(view);
  const auto root = registry_root(scope);
  std::vector<WindowsVirtualAsioRegistrationError> errors;
  append_value_error(errors, root, kClsidKey, nullptr,
                     kWindowsVirtualAsioDisplayName,
                     "virtual_asio_clsid_name_invalid",
                     "The COM class name is missing or incorrect.", access);
  append_value_error(errors, root, kInprocKey, nullptr, absolute.c_str(),
                     "virtual_asio_inproc_path_invalid",
                     "The COM in-process server path is missing or incorrect.",
                     access, true);
  append_value_error(errors, root, kInprocKey, L"ThreadingModel", L"Both",
                     "virtual_asio_threading_model_invalid",
                     "The COM threading model is missing or incorrect.", access);
  append_value_error(errors, root, kAsioKey, L"CLSID",
                     kWindowsVirtualAsioClsidString,
                     "virtual_asio_discovery_clsid_invalid",
                     "The ASIO discovery CLSID is missing or incorrect.", access);
  append_value_error(errors, root, kAsioKey, L"Description",
                     kWindowsVirtualAsioDisplayName,
                     "virtual_asio_discovery_description_invalid",
                     "The ASIO discovery description is missing or incorrect.",
                     access);
  if (!errors.empty()) {
    return WindowsVirtualAsioRegistrationResult::failure(std::move(errors));
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

WindowsVirtualAsioRegistrationResult unregister_windows_virtual_asio_driver(
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  const auto access = view_access(view);
  const auto root = registry_root(scope);
  auto native = delete_child(root, L"Software\\ASIO", L"System Audio Route", access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_discovery_delete_failed",
                          "Could not remove the ASIO discovery key.", native);
  }
  native = delete_child(root,
      L"Software\\Classes\\CLSID",
      L"{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}", access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_delete_failed",
                          "Could not remove the COM class key.", native);
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver_if_owned(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  std::wstring expected;
  const auto resolved = resolve_driver_path(dll_path, expected);
  if (!resolved.ok()) {
    return resolved;
  }

  std::wstring registered;
  const auto native = read_string(
      registry_root(scope), kInprocKey, nullptr, view_access(view), registered);
  if (native == ERROR_FILE_NOT_FOUND || native == ERROR_PATH_NOT_FOUND) {
    return WindowsVirtualAsioRegistrationResult::success();
  }
  if (native != ERROR_SUCCESS) {
    return native_failure(
        "virtual_asio_ownership_read_failed",
        "Could not determine ownership of the Virtual ASIO registration.",
        native);
  }
  if (!equal_path(registered, expected)) {
    return WindowsVirtualAsioRegistrationResult::success();
  }
  return unregister_windows_virtual_asio_driver(view, scope);
}

}  // namespace sar::driver
