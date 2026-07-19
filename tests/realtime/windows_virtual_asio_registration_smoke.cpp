#include "driver/windows_virtual_asio_com.h"
#include "driver/windows_virtual_asio_registration.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

std::wstring read_string(const wchar_t* key_path, const wchar_t* value_name) {
  DWORD bytes = 0;
  assert(RegGetValueW(HKEY_LOCAL_MACHINE, key_path, value_name,
                      RRF_RT_REG_SZ,
                      nullptr, nullptr, &bytes) == ERROR_SUCCESS);
  std::vector<wchar_t> value(bytes / sizeof(wchar_t));
  assert(RegGetValueW(HKEY_LOCAL_MACHINE, key_path, value_name,
                      RRF_RT_REG_SZ,
                      nullptr, value.data(), &bytes) == ERROR_SUCCESS);
  return value.data();
}

bool key_missing(const wchar_t* path) {
  HKEY key = nullptr;
  const auto result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0,
                                    KEY_READ, &key);
  if (key != nullptr) {
    RegCloseKey(key);
  }
  return result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND;
}

std::wstring absolute_path(const wchar_t* path) {
  const DWORD required = GetFullPathNameW(path, 0, nullptr, nullptr);
  assert(required != 0);
  std::wstring result(required, L'\0');
  const DWORD written =
      GetFullPathNameW(path, required, result.data(), nullptr);
  assert(written != 0 && written < required);
  result.resize(written);
  return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const std::wstring token = L"registration-" +
                             std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64());
  const std::wstring sandbox_path =
      L"Software\\SystemAudioRouteRegistrationTests\\" + token;
  HKEY sandbox = nullptr;
  assert(RegCreateKeyExW(HKEY_CURRENT_USER, sandbox_path.c_str(), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                         &sandbox, nullptr) == ERROR_SUCCESS);
  assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, sandbox) == ERROR_SUCCESS);

  const auto registered = sar::driver::register_windows_virtual_asio_driver(
      argv[1], sar::driver::WindowsVirtualAsioRegistryView::ProcessDefault);
  if (!registered.ok()) {
    for (const auto& error : registered.errors()) {
      std::cerr << error.code << " native=" << error.native_win32_code
                << ": " << error.message << '\n';
    }
  }
  assert(registered.ok());
  constexpr wchar_t kClsid[] =
      L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}";
  constexpr wchar_t kInproc[] =
      L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\\InprocServer32";
  constexpr wchar_t kAsio[] = L"Software\\ASIO\\System Audio Route";
  assert(read_string(kClsid, nullptr) ==
         sar::driver::kWindowsVirtualAsioDisplayName);
  assert(read_string(kInproc, nullptr) == absolute_path(argv[1]));
  assert(read_string(kInproc, L"ThreadingModel") == L"Both");
  assert(read_string(kAsio, L"CLSID") ==
         sar::driver::kWindowsVirtualAsioClsidString);
  assert(read_string(kAsio, L"Description") ==
         sar::driver::kWindowsVirtualAsioDisplayName);

  assert(sar::driver::unregister_windows_virtual_asio_driver(
             sar::driver::WindowsVirtualAsioRegistryView::ProcessDefault)
             .ok());
  assert(key_missing(kClsid));
  assert(key_missing(kAsio));
  assert(sar::driver::unregister_windows_virtual_asio_driver(
             sar::driver::WindowsVirtualAsioRegistryView::ProcessDefault)
             .ok());

  assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr) == ERROR_SUCCESS);
  RegCloseKey(sandbox);
  HKEY tests = nullptr;
  assert(RegOpenKeyExW(HKEY_CURRENT_USER,
                       L"Software\\SystemAudioRouteRegistrationTests", 0,
                       KEY_WRITE, &tests) == ERROR_SUCCESS);
  assert(RegDeleteTreeW(tests, token.c_str()) == ERROR_SUCCESS);
  RegCloseKey(tests);
}
