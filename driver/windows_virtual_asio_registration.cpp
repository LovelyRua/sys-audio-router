#include "driver/windows_virtual_asio_registration.h"

#include "driver/windows_virtual_asio_com.h"

#include <Windows.h>

#include <cwchar>
#include <string_view>
#include <utility>
#include <vector>

namespace sar::driver {
namespace {

constexpr wchar_t kAsioParentKey[] = L"Software\\ASIO";
constexpr wchar_t kClsidParentKey[] = L"Software\\Classes\\CLSID";
constexpr wchar_t kBrokerTokenValue[] = L"BrokerToken";

const WindowsVirtualAsioInstanceDescriptor& legacy_instance() {
  static const WindowsVirtualAsioInstanceDescriptor instance = {
      kWindowsVirtualAsioClsidString,
      kWindowsVirtualAsioDisplayName,
      L"System Audio Route",
      L"sys-audio-route-virtual-asio",
  };
  return instance;
}

struct ResolvedInstance {
  std::wstring clsid;
  std::wstring display_name;
  std::wstring registry_name;
  std::wstring broker_token;
  std::wstring clsid_key;
  std::wstring inproc_key;
  std::wstring asio_key;
};

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

bool contains_nul(std::wstring_view value) noexcept {
  return value.find(L'\0') != std::wstring_view::npos;
}

bool canonicalize_clsid(std::wstring_view input, std::wstring& output) {
  if (input.size() != 38 || input.front() != L'{' || input.back() != L'}') {
    return false;
  }
  output.assign(input);
  for (std::size_t index = 1; index + 1 < input.size(); ++index) {
    const bool separator =
        index == 9 || index == 14 || index == 19 || index == 24;
    if (separator) {
      if (input[index] != L'-') {
        return false;
      }
      continue;
    }
    const auto character = input[index];
    if (character >= L'0' && character <= L'9') {
      continue;
    }
    if (character >= L'a' && character <= L'f') {
      output[index] = static_cast<wchar_t>(character - L'a' + L'A');
      continue;
    }
    if (character < L'A' || character > L'F') {
      return false;
    }
  }
  return true;
}

WindowsVirtualAsioRegistrationResult resolve_instance(
    const WindowsVirtualAsioInstanceDescriptor& instance,
    ResolvedInstance& resolved) {
  if (instance.clsid.empty() || contains_nul(instance.clsid)) {
    return native_failure("virtual_asio_instance_clsid_invalid",
                          "Virtual ASIO instance CLSID is invalid.",
                          ERROR_INVALID_PARAMETER);
  }
  std::wstring canonical;
  if (!canonicalize_clsid(instance.clsid, canonical)) {
    return native_failure("virtual_asio_instance_clsid_invalid",
                          "Virtual ASIO instance CLSID is invalid.",
                          ERROR_INVALID_PARAMETER);
  }
  if (instance.display_name.empty() || contains_nul(instance.display_name)) {
    return native_failure("virtual_asio_instance_display_name_invalid",
                          "Virtual ASIO instance display name is invalid.",
                          ERROR_INVALID_PARAMETER);
  }
  if (instance.registry_name.empty() || contains_nul(instance.registry_name) ||
      instance.registry_name.find_first_of(L"\\/") != std::wstring::npos) {
    return native_failure("virtual_asio_instance_registry_name_invalid",
                          "Virtual ASIO instance registry name is invalid.",
                          ERROR_INVALID_PARAMETER);
  }
  if (instance.broker_token.empty() || contains_nul(instance.broker_token)) {
    return native_failure("virtual_asio_instance_broker_token_invalid",
                          "Virtual ASIO instance broker token is invalid.",
                          ERROR_INVALID_PARAMETER);
  }

  resolved.clsid = std::move(canonical);
  resolved.display_name = instance.display_name;
  resolved.registry_name = instance.registry_name;
  resolved.broker_token = instance.broker_token;
  resolved.clsid_key = std::wstring(kClsidParentKey) + L"\\" + resolved.clsid;
  resolved.inproc_key = resolved.clsid_key + L"\\InprocServer32";
  resolved.asio_key =
      std::wstring(kAsioParentKey) + L"\\" + resolved.registry_name;
  return WindowsVirtualAsioRegistrationResult::success();
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
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  ResolvedInstance resolved_instance;
  const auto instance_result = resolve_instance(instance, resolved_instance);
  if (!instance_result.ok()) {
    return instance_result;
  }
  std::wstring absolute;
  const auto resolved = resolve_driver_path(dll_path, absolute);
  if (!resolved.ok()) {
    return resolved;
  }

  const auto access = view_access(view);
  const auto root = registry_root(scope);
  HKEY clsid = nullptr;
  auto native = create_key(root, resolved_instance.clsid_key.c_str(), access,
                           clsid);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_key_create_failed",
                          "Could not create the COM class key.", native);
  }
  native = set_string(clsid, nullptr, resolved_instance.display_name);
  if (native != ERROR_SUCCESS) {
    RegCloseKey(clsid);
    return native_failure("virtual_asio_clsid_name_write_failed",
                          "Could not write the COM class name.", native);
  }
  native = set_string(clsid, kBrokerTokenValue,
                      resolved_instance.broker_token);
  RegCloseKey(clsid);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_broker_token_write_failed",
                          "Could not write the Virtual ASIO broker token.",
                          native);
  }

  HKEY inproc = nullptr;
  native = create_key(root, resolved_instance.inproc_key.c_str(), access,
                      inproc);
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
  native = create_key(root, resolved_instance.asio_key.c_str(), access, asio);
  if (native == ERROR_SUCCESS) {
    native = set_string(asio, L"CLSID", resolved_instance.clsid);
  }
  if (native == ERROR_SUCCESS) {
    native = set_string(asio, L"Description", resolved_instance.display_name);
  }
  if (asio != nullptr) {
    RegCloseKey(asio);
  }
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_discovery_write_failed",
                          "Could not write the ASIO discovery key.", native);
  }
  return verify_windows_virtual_asio_driver_registration(
      std::move(absolute), instance, view, scope);
}

WindowsVirtualAsioRegistrationResult register_windows_virtual_asio_driver(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  return register_windows_virtual_asio_driver(std::move(dll_path),
                                              legacy_instance(), view, scope);
}

WindowsVirtualAsioRegistrationResult
verify_windows_virtual_asio_driver_registration(
    std::wstring dll_path,
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  ResolvedInstance resolved_instance;
  const auto instance_result = resolve_instance(instance, resolved_instance);
  if (!instance_result.ok()) {
    return instance_result;
  }
  std::wstring absolute;
  const auto resolved = resolve_driver_path(dll_path, absolute);
  if (!resolved.ok()) {
    return resolved;
  }

  const auto access = view_access(view);
  const auto root = registry_root(scope);
  std::vector<WindowsVirtualAsioRegistrationError> errors;
  append_value_error(errors, root, resolved_instance.clsid_key.c_str(), nullptr,
                     resolved_instance.display_name.c_str(),
                     "virtual_asio_clsid_name_invalid",
                     "The COM class name is missing or incorrect.", access);
  append_value_error(errors, root, resolved_instance.clsid_key.c_str(),
                     kBrokerTokenValue, resolved_instance.broker_token.c_str(),
                     "virtual_asio_broker_token_invalid",
                     "The Virtual ASIO broker token is missing or incorrect.",
                     access);
  append_value_error(errors, root, resolved_instance.inproc_key.c_str(), nullptr,
                     absolute.c_str(),
                     "virtual_asio_inproc_path_invalid",
                     "The COM in-process server path is missing or incorrect.",
                     access, true);
  append_value_error(errors, root, resolved_instance.inproc_key.c_str(),
                     L"ThreadingModel", L"Both",
                     "virtual_asio_threading_model_invalid",
                     "The COM threading model is missing or incorrect.", access);
  append_value_error(errors, root, resolved_instance.asio_key.c_str(), L"CLSID",
                     resolved_instance.clsid.c_str(),
                     "virtual_asio_discovery_clsid_invalid",
                     "The ASIO discovery CLSID is missing or incorrect.", access);
  append_value_error(errors, root, resolved_instance.asio_key.c_str(),
                     L"Description", resolved_instance.display_name.c_str(),
                     "virtual_asio_discovery_description_invalid",
                     "The ASIO discovery description is missing or incorrect.",
                     access);
  if (!errors.empty()) {
    return WindowsVirtualAsioRegistrationResult::failure(std::move(errors));
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

WindowsVirtualAsioRegistrationResult
verify_windows_virtual_asio_driver_registration(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  return verify_windows_virtual_asio_driver_registration(
      std::move(dll_path), legacy_instance(), view, scope);
}

WindowsVirtualAsioRegistrationResult unregister_windows_virtual_asio_driver(
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  ResolvedInstance resolved_instance;
  const auto instance_result = resolve_instance(instance, resolved_instance);
  if (!instance_result.ok()) {
    return instance_result;
  }
  const auto access = view_access(view);
  const auto root = registry_root(scope);
  auto native = delete_child(root, kAsioParentKey,
                             resolved_instance.registry_name.c_str(), access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_discovery_delete_failed",
                          "Could not remove the ASIO discovery key.", native);
  }
  native = delete_child(root,
      kClsidParentKey, resolved_instance.clsid.c_str(), access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_delete_failed",
                          "Could not remove the COM class key.", native);
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

WindowsVirtualAsioRegistrationResult unregister_windows_virtual_asio_driver(
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  return unregister_windows_virtual_asio_driver(legacy_instance(), view, scope);
}

WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver_if_owned(
    std::wstring dll_path,
    const WindowsVirtualAsioInstanceDescriptor& instance,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  ResolvedInstance resolved_instance;
  const auto instance_result = resolve_instance(instance, resolved_instance);
  if (!instance_result.ok()) {
    return instance_result;
  }
  std::wstring expected;
  const auto resolved = resolve_driver_path(dll_path, expected);
  if (!resolved.ok()) {
    return resolved;
  }

  std::wstring registered;
  const auto native = read_string(
      registry_root(scope), resolved_instance.inproc_key.c_str(), nullptr,
      view_access(view), registered);
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
  return unregister_windows_virtual_asio_driver(instance, view, scope);
}

WindowsVirtualAsioRegistrationResult
unregister_windows_virtual_asio_driver_if_owned(
    std::wstring dll_path,
    WindowsVirtualAsioRegistryView view,
    WindowsVirtualAsioRegistrationScope scope) {
  return unregister_windows_virtual_asio_driver_if_owned(
      std::move(dll_path), legacy_instance(), view, scope);
}

}  // namespace sar::driver
