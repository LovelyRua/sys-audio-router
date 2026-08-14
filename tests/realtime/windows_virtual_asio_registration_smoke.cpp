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

std::wstring read_string(HKEY root,
                         const wchar_t* key_path,
                         const wchar_t* value_name) {
  DWORD bytes = 0;
  assert(RegGetValueW(root, key_path, value_name,
                      RRF_RT_REG_SZ,
                      nullptr, nullptr, &bytes) == ERROR_SUCCESS);
  std::vector<wchar_t> value(bytes / sizeof(wchar_t));
  assert(RegGetValueW(root, key_path, value_name,
                      RRF_RT_REG_SZ,
                      nullptr, value.data(), &bytes) == ERROR_SUCCESS);
  return value.data();
}

bool key_missing(HKEY root, const wchar_t* path) {
  HKEY key = nullptr;
  const auto result = RegOpenKeyExW(root, path, 0, KEY_READ, &key);
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

std::wstring executable_path() {
  std::wstring result(32768, L'\0');
  const auto written = GetModuleFileNameW(
      nullptr, result.data(), static_cast<DWORD>(result.size()));
  assert(written != 0 && written < result.size());
  result.resize(written);
  return result;
}

void exercise_scope(
    const wchar_t* dll_path,
    sar::driver::WindowsVirtualAsioRegistrationScope scope) {
  using sar::driver::WindowsVirtualAsioInstanceDescriptor;
  using sar::driver::WindowsVirtualAsioRegistryView;
  constexpr wchar_t kClsid[] =
      L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}";
  constexpr wchar_t kInproc[] =
      L"Software\\Classes\\CLSID\\{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}\\InprocServer32";
  constexpr wchar_t kAsio[] = L"Software\\ASIO\\System Audio Route";
  const WindowsVirtualAsioInstanceDescriptor instance_a = {
      L"{83d4c47a-9834-41f4-a5ee-62bfb8f28d8a}",
      L"System Audio Route Test A",
      L"System Audio Route Test A",
      L"registration-instance-a",
  };
  const WindowsVirtualAsioInstanceDescriptor instance_b = {
      L"{11C53C37-D8A7-48F5-8678-055E10E45462}",
      L"System Audio Route Test B",
      L"System Audio Route Test B",
      L"registration-instance-b",
  };
  constexpr wchar_t kClsidA[] =
      L"Software\\Classes\\CLSID\\{83D4C47A-9834-41F4-A5EE-62BFB8F28D8A}";
  constexpr wchar_t kInprocA[] =
      L"Software\\Classes\\CLSID\\{83D4C47A-9834-41F4-A5EE-62BFB8F28D8A}\\InprocServer32";
  constexpr wchar_t kAsioA[] = L"Software\\ASIO\\System Audio Route Test A";
  constexpr wchar_t kClsidB[] =
      L"Software\\Classes\\CLSID\\{11C53C37-D8A7-48F5-8678-055E10E45462}";
  constexpr wchar_t kAsioB[] = L"Software\\ASIO\\System Audio Route Test B";
  const auto root = scope ==
                            sar::driver::WindowsVirtualAsioRegistrationScope::CurrentUser
                        ? HKEY_CURRENT_USER
                        : HKEY_LOCAL_MACHINE;

  const auto registered = sar::driver::register_windows_virtual_asio_driver(
      dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope);
  if (!registered.ok()) {
    for (const auto& error : registered.errors()) {
      std::cerr << error.code << " native=" << error.native_win32_code
                << ": " << error.message << '\n';
    }
  }
  assert(registered.ok());
  assert(sar::driver::verify_windows_virtual_asio_driver_registration(
             dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(read_string(root, kClsid, nullptr) ==
         sar::driver::kWindowsVirtualAsioDisplayName);
  assert(read_string(root, kClsid, L"BrokerToken") ==
         L"sys-audio-route-virtual-asio");
  assert(read_string(root, kInproc, nullptr) == absolute_path(dll_path));
  assert(read_string(root, kInproc, L"ThreadingModel") == L"Both");
  assert(read_string(root, kAsio, L"CLSID") ==
         sar::driver::kWindowsVirtualAsioClsidString);
  assert(read_string(root, kAsio, L"Description") ==
         sar::driver::kWindowsVirtualAsioDisplayName);

  HKEY asio = nullptr;
  assert(RegOpenKeyExW(root, kAsio, 0, KEY_SET_VALUE, &asio) == ERROR_SUCCESS);
  constexpr wchar_t invalid[] = L"Invalid";
  assert(RegSetValueExW(asio, L"Description", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(invalid),
                        sizeof(invalid)) == ERROR_SUCCESS);
  RegCloseKey(asio);
  assert(!sar::driver::verify_windows_virtual_asio_driver_registration(
              dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
              .ok());
  assert(sar::driver::register_windows_virtual_asio_driver(
             dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());

  assert(sar::driver::register_windows_virtual_asio_driver(
             dll_path, instance_a,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(sar::driver::register_windows_virtual_asio_driver(
             dll_path, instance_b,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(read_string(root, kClsidA, nullptr) == instance_a.display_name);
  assert(read_string(root, kClsidA, L"BrokerToken") ==
         instance_a.broker_token);
  assert(read_string(root, kInprocA, nullptr) == absolute_path(dll_path));
  assert(read_string(root, kAsioA, L"CLSID") ==
         L"{83D4C47A-9834-41F4-A5EE-62BFB8F28D8A}");
  assert(read_string(root, kAsioA, L"Description") ==
         instance_a.display_name);
  assert(read_string(root, kClsidB, L"BrokerToken") ==
         instance_b.broker_token);

  assert(sar::driver::unregister_windows_virtual_asio_driver(
             instance_a, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(key_missing(root, kClsidA));
  assert(key_missing(root, kAsioA));
  assert(sar::driver::verify_windows_virtual_asio_driver_registration(
             dll_path, instance_b,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());

  HKEY clsid_b = nullptr;
  assert(RegOpenKeyExW(root, kClsidB, 0, KEY_SET_VALUE, &clsid_b) ==
         ERROR_SUCCESS);
  assert(RegSetValueExW(clsid_b, L"BrokerToken", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(invalid),
                        sizeof(invalid)) == ERROR_SUCCESS);
  RegCloseKey(clsid_b);
  assert(!sar::driver::verify_windows_virtual_asio_driver_registration(
              dll_path, instance_b,
              WindowsVirtualAsioRegistryView::ProcessDefault, scope)
              .ok());
  assert(sar::driver::register_windows_virtual_asio_driver(
             dll_path, instance_b,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());

  assert(sar::driver::unregister_windows_virtual_asio_driver_if_owned(
             executable_path(), instance_b,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(!key_missing(root, kClsidB));
  assert(sar::driver::unregister_windows_virtual_asio_driver_if_owned(
             dll_path, instance_b,
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(key_missing(root, kClsidB));
  assert(key_missing(root, kAsioB));

  assert(sar::driver::unregister_windows_virtual_asio_driver_if_owned(
             executable_path(), WindowsVirtualAsioRegistryView::ProcessDefault,
             scope)
             .ok());
  assert(sar::driver::verify_windows_virtual_asio_driver_registration(
             dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());

  assert(RegOpenKeyExW(root, kAsio, 0, KEY_SET_VALUE, &asio) == ERROR_SUCCESS);
  assert(RegSetValueExW(asio, L"Description", 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(invalid),
                        sizeof(invalid)) == ERROR_SUCCESS);
  RegCloseKey(asio);
  assert(sar::driver::unregister_windows_virtual_asio_driver_if_owned(
             dll_path, WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(key_missing(root, kClsid));
  assert(key_missing(root, kAsio));

  assert(sar::driver::unregister_windows_virtual_asio_driver(
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
  assert(key_missing(root, kClsid));
  assert(key_missing(root, kAsio));
  assert(sar::driver::unregister_windows_virtual_asio_driver(
             WindowsVirtualAsioRegistryView::ProcessDefault, scope)
             .ok());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const sar::driver::WindowsVirtualAsioInstanceDescriptor invalid_instance = {
      L"not-a-guid", L"Invalid", L"Invalid", L"invalid"};
  const sar::driver::WindowsVirtualAsioInstanceDescriptor invalid_name = {
      L"{83D4C47A-9834-41F4-A5EE-62BFB8F28D8A}", L"Invalid",
      L"Invalid\\Nested", L"invalid"};
  assert(!sar::driver::register_windows_virtual_asio_driver(
              argv[1], invalid_instance,
              sar::driver::WindowsVirtualAsioRegistryView::ProcessDefault,
              sar::driver::WindowsVirtualAsioRegistrationScope::CurrentUser)
              .ok());
  assert(!sar::driver::register_windows_virtual_asio_driver(
              argv[1], invalid_name,
              sar::driver::WindowsVirtualAsioRegistryView::ProcessDefault,
              sar::driver::WindowsVirtualAsioRegistrationScope::CurrentUser)
              .ok());
  const std::wstring token = L"registration-" +
                             std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64());
  const std::wstring sandbox_path =
      L"Software\\SystemAudioRouteRegistrationTests\\" + token;
  HKEY sandbox = nullptr;
  assert(RegCreateKeyExW(HKEY_CURRENT_USER, sandbox_path.c_str(), 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                         &sandbox, nullptr) == ERROR_SUCCESS);
  HKEY machine_sandbox = nullptr;
  HKEY user_sandbox = nullptr;
  assert(RegCreateKeyExW(sandbox, L"Machine", 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                         &machine_sandbox, nullptr) == ERROR_SUCCESS);
  assert(RegCreateKeyExW(sandbox, L"User", 0, nullptr,
                         REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                         &user_sandbox, nullptr) == ERROR_SUCCESS);

  assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, machine_sandbox) ==
         ERROR_SUCCESS);
  exercise_scope(argv[1],
                 sar::driver::WindowsVirtualAsioRegistrationScope::LocalMachine);
  assert(RegOverridePredefKey(HKEY_LOCAL_MACHINE, nullptr) == ERROR_SUCCESS);

  assert(RegOverridePredefKey(HKEY_CURRENT_USER, user_sandbox) == ERROR_SUCCESS);
  exercise_scope(argv[1],
                 sar::driver::WindowsVirtualAsioRegistrationScope::CurrentUser);
  assert(RegOverridePredefKey(HKEY_CURRENT_USER, nullptr) == ERROR_SUCCESS);

  RegCloseKey(user_sandbox);
  RegCloseKey(machine_sandbox);
  RegCloseKey(sandbox);
  HKEY tests = nullptr;
  assert(RegOpenKeyExW(HKEY_CURRENT_USER,
                       L"Software\\SystemAudioRouteRegistrationTests", 0,
                       KEY_WRITE, &tests) == ERROR_SUCCESS);
  assert(RegDeleteTreeW(tests, token.c_str()) == ERROR_SUCCESS);
  RegCloseKey(tests);
}
