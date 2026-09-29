#include "app/gui_vstgui/main_window.h"

#include "vstgui/standalone/include/helpers/appdelegate.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/iapplication.h"

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

struct AppDelegate final : Application::DelegateAdapter, WindowListenerAdapter {
  AppDelegate()
      : DelegateAdapter({"System Audio Route", SAR_VERSION,
                         "com.systemaudioroute.controlpanel"}) {}

  void finishLaunching() override {
    auto window = createMainWindow();
    if (!window) {
      IApplication::instance().quit();
      return;
    }
    window->show();
    window->registerWindowListener(this);
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
