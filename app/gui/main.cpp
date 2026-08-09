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

namespace {

class GuiInstanceGuard final {
 public:
  GuiInstanceGuard() noexcept {
    mutex_ = CreateMutexW(
        nullptr, FALSE,
        L"Local\\SystemAudioRoute.Gui.{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}");
    if (mutex_ == nullptr) {
      return;
    }

    const auto wait_result = WaitForSingleObject(mutex_, 0);
    primary_ = wait_result == WAIT_OBJECT_0 || wait_result == WAIT_ABANDONED;
  }
  GuiInstanceGuard(const GuiInstanceGuard&) = delete;
  GuiInstanceGuard& operator=(const GuiInstanceGuard&) = delete;
  ~GuiInstanceGuard() {
    if (mutex_ != nullptr) {
      if (primary_) {
        ReleaseMutex(mutex_);
      }
      CloseHandle(mutex_);
    }
  }

  [[nodiscard]] bool primary() const noexcept { return primary_; }
  [[nodiscard]] bool valid() const noexcept { return mutex_ != nullptr; }

 private:
  HANDLE mutex_ = nullptr;
  bool primary_ = false;
};

}  // namespace
#endif

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("System Audio Route"));
  QGuiApplication::setOrganizationName(QStringLiteral("System Audio Route"));
  QQuickStyle::setStyle(QStringLiteral("Basic"));

#ifdef Q_OS_WIN
  GuiInstanceGuard instance;
  if (!instance.valid()) {
    return 1;
  }
  if (!instance.primary()) {
    return 0;
  }
#endif

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
