#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJSValue>
#include <QTextStream>

#include <cstdio>

namespace {

bool evaluate(QJSEngine& engine, const QString& source, const QString& name) {
  const auto result = engine.evaluate(source, name);
  if (result.isError()) {
    QTextStream(stderr) << name << ':' << result.property("lineNumber").toInt()
                        << ": " << result.toString() << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (argc != 3) return 2;
  QFile file(QString::fromLocal8Bit(argv[1]));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 2;
  const auto source = QString::fromUtf8(file.readAll());
  if (!source.startsWith(".pragma library\n")) return 2;

  QJSEngine engine;
  if (!evaluate(engine, source.mid(source.indexOf('\n') + 1),
                QStringLiteral("MatrixProjection.js"))) return 1;

  QFile scenarios_file(QString::fromLocal8Bit(argv[2]));
  if (!scenarios_file.open(QIODevice::ReadOnly | QIODevice::Text)) return 2;
  const auto scenarios = QString::fromUtf8(scenarios_file.readAll());
  return evaluate(engine, scenarios, QStringLiteral("matrix-projection-scenarios"))
             ? 0 : 1;
}
