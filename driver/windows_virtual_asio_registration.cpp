#include "driver/windows_virtual_asio_registration.h"

#include "driver/windows_virtual_asio_com.h"

#include <Windows.h>

#include <utility>

namespace sar::driver {
namespace {

constexpr wchar_t kAsioKey[] =
    L"Software\\ASIO\\System Audio Route";
constexpr wchar_t kClsidKey[] =
    L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}";
constexpr wchar_t kInprocKey[] =
    L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\\InprocServer32";

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

LSTATUS create_key(const wchar_t* path,
                   REGSAM access,
                   HKEY& key) noexcept {
  return RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_WRITE | access, nullptr,
                         &key, nullptr);
}

LSTATUS delete_child(const wchar_t* parent,
                     const wchar_t* child,
                     REGSAM access) noexcept {
  HKEY key = nullptr;
  const auto opened = RegOpenKeyExW(HKEY_LOCAL_MACHINE, parent, 0,
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
    WindowsVirtualAsioRegistryView view) {
  if (dll_path.empty()) {
    return native_failure("virtual_asio_dll_path_empty",
                          "Virtual ASIO DLL path must not be empty.",
                          ERROR_INVALID_PARAMETER);
  }
  const DWORD required = GetFullPathNameW(
      dll_path.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return native_failure("virtual_asio_dll_path_invalid",
                          "Could not resolve the Virtual ASIO DLL path.",
                          GetLastError());
  }
  std::wstring absolute(required, L'\0');
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

  const auto access = view_access(view);
  HKEY clsid = nullptr;
  auto native = create_key(kClsidKey, access, clsid);
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
  native = create_key(kInprocKey, access, inproc);
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
  native = create_key(kAsioKey, access, asio);
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
  return WindowsVirtualAsioRegistrationResult::success();
}

WindowsVirtualAsioRegistrationResult unregister_windows_virtual_asio_driver(
    WindowsVirtualAsioRegistryView view) {
  const auto access = view_access(view);
  auto native = delete_child(L"Software\\ASIO", L"System Audio Route", access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_discovery_delete_failed",
                          "Could not remove the ASIO discovery key.", native);
  }
  native = delete_child(
      L"Software\\Classes\\CLSID",
      L"{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}", access);
  if (native != ERROR_SUCCESS) {
    return native_failure("virtual_asio_clsid_delete_failed",
                          "Could not remove the COM class key.", native);
  }
  return WindowsVirtualAsioRegistrationResult::success();
}

}  // namespace sar::driver
