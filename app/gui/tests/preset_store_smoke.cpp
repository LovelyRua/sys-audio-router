#include "app/gui/preset_store.h"

#include <QFile>
#include <QTemporaryDir>

#include <cassert>

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48'000;
  preset.frames_per_block = 128;
  preset.nodes = {{"matrix", "Main Matrix", "route_matrix"}};
  preset.matrix.inputs = {{"daw-1", "DAW 1"}};
  preset.matrix.outputs = {{"monitor-1", "Monitor 1"}};
  preset.matrix.routes = {{"daw-1", "monitor-1", 0.75F, false}};
  return preset;
}

}  // namespace

int main() {
  QTemporaryDir temporary_directory;
  assert(temporary_directory.isValid());

  const sar::gui::PresetStore store(temporary_directory.path());
  QString error;
  assert(store.save(QStringLiteral("Tracking"), make_preset(), &error));
  assert(error.isEmpty());
  assert(store.names(&error) == QStringList{QStringLiteral("Tracking")});

  sar::control::PresetDocument loaded;
  assert(store.load(QStringLiteral("Tracking"), &loaded, &error));
  assert(loaded.sample_rate == 48'000);
  assert(loaded.frames_per_block == 128);
  assert(loaded.nodes.size() == 1);
  assert(loaded.matrix.inputs.front().id == "daw-1");
  assert(loaded.matrix.outputs.front().id == "monitor-1");
  assert(loaded.matrix.routes.front().gain == 0.75F);
  assert(!loaded.matrix.routes.front().muted);

  assert(!store.save(QStringLiteral("../escape"), make_preset(), &error));
  assert(!error.isEmpty());
  assert(store.save(QStringLiteral("Recovered"), make_preset(), &error));
  assert(error.isEmpty());

  QFile malformed(temporary_directory.filePath(
      QStringLiteral("Broken.sarpreset")));
  assert(malformed.open(QIODevice::WriteOnly));
  assert(malformed.write("{ definitely not json") > 0);
  malformed.close();
  assert(!store.load(QStringLiteral("Broken"), &loaded, &error));
  assert(error.contains(QStringLiteral("invalid"), Qt::CaseInsensitive));
  return 0;
}
