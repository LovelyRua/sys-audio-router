#include "driver/windows_virtual_asio_registration.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <RestartManager.h>

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

void print_usage() {
  std::cerr
      << "Usage: sar_virtual_asio_register "
         "(--register [DLL]|--verify [DLL]|--unregister|"
         "--unregister-owned [DLL]|--check-unlocked [DLL]) "
         "[--user|--machine] [--x64|--x86] "
         "[--clsid GUID --display-name NAME --registry-name NAME "
         "--broker-token TOKEN]\n"
         "DLL defaults to SystemAudioRouteVirtualASIO.dll beside this tool.\n";
}

int check_driver_unlocked(const std::wstring& dll_path) {
  DWORD session = 0;
  wchar_t session_key[CCH_RM_SESSION_KEY + 1]{};
  const auto start_error = RmStartSession(&session, 0, session_key);
  if (start_error != ERROR_SUCCESS) {
    std::wcerr << L"asio_lock_check_failed: RmStartSession native="
               << start_error << L'\n';
    return 1;
  }
  struct SessionGuard {
    DWORD value;
    ~SessionGuard() { RmEndSession(value); }
  } guard{session};

  const wchar_t* resources[] = {dll_path.c_str()};
  const auto register_error =
      RmRegisterResources(session, 1, resources, 0, nullptr, 0, nullptr);
  if (register_error != ERROR_SUCCESS) {
    std::wcerr << L"asio_lock_check_failed: RmRegisterResources native="
               << register_error << L'\n';
    return 1;
  }

  UINT required = 0;
  UINT count = 0;
  DWORD reboot_reasons = 0;
  auto list_error = RmGetList(session, &required, &count, nullptr,
                              &reboot_reasons);
  if (list_error == ERROR_SUCCESS && required == 0) {
    std::cout << "virtual_asio_driver=unlocked\n";
    return 0;
  }
  if (list_error != ERROR_MORE_DATA) {
    std::wcerr << L"asio_lock_check_failed: RmGetList native=" << list_error
               << L'\n';
    return 1;
  }

  std::vector<RM_PROCESS_INFO> processes(required);
  count = required;
  list_error = RmGetList(session, &required, &count, processes.data(),
                         &reboot_reasons);
  if (list_error != ERROR_SUCCESS) {
    std::wcerr << L"asio_lock_check_failed: RmGetList native=" << list_error
               << L'\n';
    return 1;
  }
  for (UINT index = 0; index < count; ++index) {
    std::wcerr << L"asio_driver_locked_by: "
               << processes[index].strAppName << L" pid="
               << processes[index].Process.dwProcessId << L'\n';
  }
  return count == 0 ? 0 : 3;
}

bool is_option(const wchar_t* value) noexcept {
  return value != nullptr && value[0] == L'-';
}

std::wstring adjacent_driver_path() {
  std::wstring executable(32768, L'\0');
  const DWORD written = GetModuleFileNameW(
      nullptr, executable.data(), static_cast<DWORD>(executable.size()));
  if (written == 0 || written >= executable.size()) {
    return {};
  }
  executable.resize(written);
  const auto separator = executable.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return L"SystemAudioRouteVirtualASIO.dll";
  }
  executable.resize(separator + 1);
  executable += L"SystemAudioRouteVirtualASIO.dll";
  return executable;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  using sar::driver::WindowsVirtualAsioRegistrationScope;
  using sar::driver::WindowsVirtualAsioRegistryView;
  bool register_driver = false;
  bool verify_driver = false;
  bool unregister_driver = false;
  bool unregister_owned_driver = false;
  bool check_unlocked = false;
  WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64;
  WindowsVirtualAsioRegistrationScope scope =
      WindowsVirtualAsioRegistrationScope::LocalMachine;
  std::wstring dll_path;
  sar::driver::WindowsVirtualAsioInstanceDescriptor instance;

  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if (argument == L"--register") {
      register_driver = true;
      if (index + 1 < argc && !is_option(argv[index + 1])) {
        dll_path = argv[++index];
      }
    } else if (argument == L"--verify") {
      verify_driver = true;
      if (index + 1 < argc && !is_option(argv[index + 1])) {
        dll_path = argv[++index];
      }
    } else if (argument == L"--unregister") {
      unregister_driver = true;
    } else if (argument == L"--unregister-owned") {
      unregister_owned_driver = true;
      if (index + 1 < argc && !is_option(argv[index + 1])) {
        dll_path = argv[++index];
      }
    } else if (argument == L"--check-unlocked") {
      check_unlocked = true;
      if (index + 1 < argc && !is_option(argv[index + 1])) {
        dll_path = argv[++index];
      }
    } else if (argument == L"--x86") {
      view = WindowsVirtualAsioRegistryView::X86;
    } else if (argument == L"--x64") {
      view = WindowsVirtualAsioRegistryView::X64;
    } else if (argument == L"--user") {
      scope = WindowsVirtualAsioRegistrationScope::CurrentUser;
    } else if (argument == L"--machine") {
      scope = WindowsVirtualAsioRegistrationScope::LocalMachine;
    } else if (argument == L"--clsid" && index + 1 < argc) {
      instance.clsid = argv[++index];
    } else if (argument == L"--display-name" && index + 1 < argc) {
      instance.display_name = argv[++index];
    } else if (argument == L"--registry-name" && index + 1 < argc) {
      instance.registry_name = argv[++index];
    } else if (argument == L"--broker-token" && index + 1 < argc) {
      instance.broker_token = argv[++index];
    } else if (argument == L"--help" || argument == L"-h") {
      print_usage();
      return 0;
    } else {
      print_usage();
      return 2;
    }
  }
  const int action_count = static_cast<int>(register_driver) +
                           static_cast<int>(verify_driver) +
                           static_cast<int>(unregister_driver) +
                           static_cast<int>(unregister_owned_driver) +
                           static_cast<int>(check_unlocked);
  if (action_count != 1) {
    std::cerr << "Choose exactly one registration action.\n";
    return 2;
  }
  const auto instance_field_count = static_cast<int>(!instance.clsid.empty()) +
                                    static_cast<int>(!instance.display_name.empty()) +
                                    static_cast<int>(!instance.registry_name.empty()) +
                                    static_cast<int>(!instance.broker_token.empty());
  if (instance_field_count != 0 && instance_field_count != 4) {
    std::cerr << "Specify all four Virtual ASIO instance identity options.\n";
    return 2;
  }
  const bool custom_instance = instance_field_count == 4;
  if (custom_instance && check_unlocked) {
    std::cerr << "Instance identity options do not apply to --check-unlocked.\n";
    return 2;
  }
  if ((register_driver || verify_driver || unregister_owned_driver ||
       check_unlocked) &&
      dll_path.empty()) {
    dll_path = adjacent_driver_path();
  }
  if (check_unlocked) {
    return check_driver_unlocked(dll_path);
  }

  const auto result = custom_instance
      ? register_driver
            ? sar::driver::register_windows_virtual_asio_driver(
                  std::move(dll_path), instance, view, scope)
            : verify_driver
                  ? sar::driver::verify_windows_virtual_asio_driver_registration(
                        std::move(dll_path), instance, view, scope)
                  : unregister_owned_driver
                        ? sar::driver::unregister_windows_virtual_asio_driver_if_owned(
                              std::move(dll_path), instance, view, scope)
                        : sar::driver::unregister_windows_virtual_asio_driver(
                              instance, view, scope)
      : register_driver
            ? sar::driver::register_windows_virtual_asio_driver(
                  std::move(dll_path), view, scope)
            : verify_driver
                  ? sar::driver::verify_windows_virtual_asio_driver_registration(
                        std::move(dll_path), view, scope)
                  : unregister_owned_driver
                        ? sar::driver::unregister_windows_virtual_asio_driver_if_owned(
                              std::move(dll_path), view, scope)
                        : sar::driver::unregister_windows_virtual_asio_driver(
                              view, scope);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message
                << " native=" << error.native_win32_code << '\n';
    }
    return 1;
  }
  std::cout << "virtual_asio_registration="
            << (register_driver ? "registered"
                                : verify_driver ? "verified"
                                                : "unregistered")
            << " scope="
            << (scope == WindowsVirtualAsioRegistrationScope::CurrentUser
                    ? "user" : "machine")
            << " view="
            << (view == WindowsVirtualAsioRegistryView::X86 ? "x86" : "x64")
            << '\n';
}
