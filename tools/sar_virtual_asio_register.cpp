#include "driver/windows_virtual_asio_registration.h"

#include <iostream>
#include <string>
#include <utility>

int wmain(int argc, wchar_t** argv) {
  using sar::driver::WindowsVirtualAsioRegistryView;
  bool register_driver = false;
  bool unregister_driver = false;
  WindowsVirtualAsioRegistryView view = WindowsVirtualAsioRegistryView::X64;
  std::wstring dll_path;

  for (int index = 1; index < argc; ++index) {
    const std::wstring argument = argv[index];
    if (argument == L"--register" && index + 1 < argc) {
      register_driver = true;
      dll_path = argv[++index];
    } else if (argument == L"--unregister") {
      unregister_driver = true;
    } else if (argument == L"--x86") {
      view = WindowsVirtualAsioRegistryView::X86;
    } else if (argument == L"--x64") {
      view = WindowsVirtualAsioRegistryView::X64;
    } else {
      std::cerr << "Usage: sar_virtual_asio_register "
                   "(--register DLL|--unregister) [--x64|--x86]\n";
      return 2;
    }
  }
  if (register_driver == unregister_driver) {
    std::cerr << "Choose exactly one of --register or --unregister.\n";
    return 2;
  }

  const auto result = register_driver
                          ? sar::driver::register_windows_virtual_asio_driver(
                                std::move(dll_path), view)
                          : sar::driver::unregister_windows_virtual_asio_driver(
                                view);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message
                << " native=" << error.native_win32_code << '\n';
    }
    return 1;
  }
  std::cout << "virtual_asio_registration="
            << (register_driver ? "registered" : "unregistered") << '\n';
}
