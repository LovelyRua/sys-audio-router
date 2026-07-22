#include "app/gui/engine_controller.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

int main(int argc, char* argv[]) {
  QGuiApplication application(argc, argv);
  QGuiApplication::setApplicationName(QStringLiteral("System Audio Route"));
  QGuiApplication::setOrganizationName(QStringLiteral("System Audio Route"));
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
