#include "app/gui_vstgui/main_window.h"
#include "app/gui_vstgui/virtual_device_window.h"
#include "app/gui_vstgui/runtime_device_window.h"
#include "app/gui_vstgui/preset_window.h"
#include "app/gui_vstgui/diagnostics_window.h"
#include "app/gui_vstgui/asio_device_window.h"
#include "app/gui_vstgui/localization.h"

#include "vstgui/standalone/include/helpers/appdelegate.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/lib/cvstguitimer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <string_view>
#include <cstdio>

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

struct AppDelegate final : Application::DelegateAdapter, WindowListenerAdapter {
  AppDelegate()
      : DelegateAdapter({"System Audio Route", SAR_VERSION,
                         "com.systemaudioroute.controlpanel"}) {}

  void finishLaunching() override {
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool startupSmoke = false;
    if (arguments) {
      for (int i = 1; i < count; ++i)
        startupSmoke = startupSmoke || std::wstring_view(arguments[i]) == L"--startup-smoke";
      LocalFree(arguments);
    }
    if (startupSmoke) std::fprintf(stderr, "native_smoke phase=initialize tick=%llu\n", GetTickCount64());
    initializeLanguage();
    auto window = createMainWindow();
    if (!window) {
      IApplication::instance().quit();
      return;
    }
    window->show();
    window->registerWindowListener(this);
    if (startupSmoke) {
      std::fprintf(stderr, "native_smoke phase=main_shown tick=%llu\n", GetTickCount64());
      const auto open = [](const char* name, void (*show)()) {
        std::fprintf(stderr, "native_smoke phase=open_begin window=%s tick=%llu\n", name, GetTickCount64());
        show();
        std::fprintf(stderr, "native_smoke phase=open_end window=%s tick=%llu\n", name, GetTickCount64());
        std::fflush(stderr);
      };
      Call::later([open] { open("driver_lab", showVirtualDeviceWindow); }, 500);
      Call::later([open] { open("audio_devices", showRuntimeDeviceWindow); }, 1000);
      Call::later([open] { open("presets", showPresetWindow); }, 1500);
      Call::later([open] { open("diagnostics", showDiagnosticsWindow); }, 2000);
      Call::later([open] { open("virtual_asio", showAsioDeviceWindow); }, 2500);
      Call::later([] {
        const auto windows = IApplication::instance().getWindows();
        std::fprintf(stderr, "native_smoke phase=close_begin count=%zu tick=%llu\n", windows.size(), GetTickCount64());
        for (const auto& current : windows) {
          std::fprintf(stderr, "native_smoke phase=close_window title=%s\n", current->getTitle().getString().c_str());
          current->close();
        }
        std::fprintf(stderr, "native_smoke phase=close_end remaining=%zu tick=%llu\n",
            IApplication::instance().getWindows().size(), GetTickCount64());
        std::fflush(stderr);
      }, 6500);
    }
  }

  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) {
      IApplication::instance().quit();
    }
  }
};

}  // namespace
}  // namespace sar::gui_vstgui

static Application::Init gAppDelegate(
    std::make_unique<sar::gui_vstgui::AppDelegate>());
