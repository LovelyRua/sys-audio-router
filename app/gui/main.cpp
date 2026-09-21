#include "app/gui/engine_controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cwchar>

namespace {

class GuiInstanceGuard final {
 public:
  GuiInstanceGuard() noexcept {
    event_ = CreateEventW(
        nullptr, TRUE, FALSE,
        L"Local\\SystemAudioRoute.Gui.{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}");
    const auto creation_error = GetLastError();
    primary_ = event_ != nullptr && creation_error != ERROR_ALREADY_EXISTS;
  }
  GuiInstanceGuard(const GuiInstanceGuard&) = delete;
  GuiInstanceGuard& operator=(const GuiInstanceGuard&) = delete;
  ~GuiInstanceGuard() {
    if (event_ != nullptr) {
      CloseHandle(event_);
    }
  }

  [[nodiscard]] bool primary() const noexcept { return primary_; }
  [[nodiscard]] bool valid() const noexcept { return event_ != nullptr; }

 private:
  HANDLE event_ = nullptr;
  bool primary_ = false;
};

BOOL CALLBACK activate_matching_window(HWND window, LPARAM found) {
  wchar_t title[64] = {};
  wchar_t class_name[64] = {};
  if (!IsWindowVisible(window) ||
      GetWindowTextW(window, title, 64) == 0 ||
      GetClassNameW(window, class_name, 64) == 0) {
    return TRUE;
  }
  if (wcscmp(title, L"System Audio Route") != 0 ||
      wcsncmp(class_name, L"Qt", 2) != 0) {
    return TRUE;
  }
  ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
  SetForegroundWindow(window);
  *reinterpret_cast<bool*>(found) = true;
  return FALSE;
}

// A second launch brings the running control panel forward instead of
// silently doing nothing.
void activate_existing_window() noexcept {
  bool found = false;
  EnumWindows(activate_matching_window, reinterpret_cast<LPARAM>(&found));
}

}  // namespace
#endif

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
  GuiInstanceGuard instance;
  if (!instance.valid()) {
    return 1;
  }
  if (!instance.primary()) {
    activate_existing_window();
    return 0;
  }
#endif

  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("System Audio Route"));
  QGuiApplication::setOrganizationName(QStringLiteral("System Audio Route"));
  QGuiApplication::setApplicationVersion(QStringLiteral(SAR_VERSION));
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  sar::gui::EngineController engine_controller;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("engine"),
                                            &engine_controller);
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                   &application, [] { QCoreApplication::exit(1); },
                   Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("Sar.Gui"), QStringLiteral("Main"));
  return application.exec();
}
