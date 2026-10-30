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

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

struct AppDelegate final : Application::DelegateAdapter, WindowListenerAdapter {
  AppDelegate()
      : DelegateAdapter({"System Audio Route", SAR_VERSION,
                         "com.systemaudioroute.controlpanel"}) {}

  void finishLaunching() override {
    initializeLanguage();
    auto window = createMainWindow();
    if (!window) {
      IApplication::instance().quit();
      return;
    }
    window->show();
    window->registerWindowListener(this);
    int count = 0;
    auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool startupSmoke = false;
    if (arguments) {
      for (int i = 1; i < count; ++i)
        startupSmoke = startupSmoke || std::wstring_view(arguments[i]) == L"--startup-smoke";
      LocalFree(arguments);
    }
    if (startupSmoke) {
      Call::later([] { showVirtualDeviceWindow(); }, 500);
      Call::later([] { showRuntimeDeviceWindow(); }, 1000);
      Call::later([] { showPresetWindow(); }, 1500);
      Call::later([] { showDiagnosticsWindow(); }, 2000);
      Call::later([] { showAsioDeviceWindow(); }, 2500);
      Call::later([] {
        const auto windows = IApplication::instance().getWindows();
        for (const auto& current : windows) current->close();
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
