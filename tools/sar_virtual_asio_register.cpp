#include "driver/windows_virtual_asio_registration.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <iostream>
#include <string>
#include <utility>

namespace {

void print_usage() {
  std::cerr
      << "Usage: sar_virtual_asio_register "
         "(--register [DLL]|--verify [DLL]|--unregister|"
         "--unregister-owned [DLL]) "
         "[--user|--machine] [--x64|--x86]\n"
         "DLL defaults to SystemAudioRouteVirtualASIO.dll beside this tool.\n";
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
  WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64;
  WindowsVirtualAsioRegistrationScope scope =
      WindowsVirtualAsioRegistrationScope::LocalMachine;
  std::wstring dll_path;

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
    } else if (argument == L"--x86") {
      view = WindowsVirtualAsioRegistryView::X86;
    } else if (argument == L"--x64") {
      view = WindowsVirtualAsioRegistryView::X64;
    } else if (argument == L"--user") {
      scope = WindowsVirtualAsioRegistrationScope::CurrentUser;
    } else if (argument == L"--machine") {
      scope = WindowsVirtualAsioRegistrationScope::LocalMachine;
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
                           static_cast<int>(unregister_owned_driver);
  if (action_count != 1) {
    std::cerr << "Choose exactly one registration action.\n";
    return 2;
  }
  if ((register_driver || verify_driver || unregister_owned_driver) &&
      dll_path.empty()) {
    dll_path = adjacent_driver_path();
  }

  const auto result = register_driver
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
