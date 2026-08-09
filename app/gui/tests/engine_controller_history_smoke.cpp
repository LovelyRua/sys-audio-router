#include "app/gui/engine_controller.h"
#include "app/gui/preset_store.h"

#include "core/control/control_command.h"
#include "core/control/control_response.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QStandardPaths>
#include <QThread>

#include <cassert>
#include <cmath>
#include <mutex>

namespace {

using sar::control::ControlCommand;
using sar::control::ControlCommandType;
using sar::control::PresetDocument;
using sar::gui::EngineReply;

PresetDocument preset_with_gain(float gain) {
  PresetDocument preset;
  preset.sample_rate = 48'000;
  preset.frames_per_block = 128;
  preset.matrix.inputs = {{"daw", "DAW"}};
  preset.matrix.outputs = {{"monitor", "Monitor"}};
  preset.matrix.routes = {{"daw", "monitor", gain, false}};
  return preset;
}

EngineReply accepted(const ControlCommand& command) {
  EngineReply reply;
  reply.request_type = command.type;
  reply.transport_ok = true;
  reply.response.command_id = command.command_id;
  return reply;
}

EngineReply rejected(const ControlCommand& command) {
  auto reply = accepted(command);
  reply.response = sar::control::command_rejected(
      command.command_id, {{"test_rejection", "Injected history failure"}});
  reply.error = QStringLiteral("Injected history failure");
  return reply;
}

EngineReply session_state(const ControlCommand& command,
                          const PresetDocument& preset) {
  auto reply = accepted(command);
  reply.response.has_session_state = true;
  reply.response.has_preset = true;
  reply.response.preset = preset;
  return reply;
}

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 5000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

bool near(double lhs, double rhs) {
  return std::abs(lhs - rhs) < 0.0001;
}

}  // namespace

int main(int argc, char** argv) {
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication application(argc, argv);

  std::mutex mutex;
  auto engine_preset = preset_with_gain(1.0F);
  bool reject_next_history_load = false;
  bool reject_next_mutation = false;
  const auto transport = [&](ControlCommand command) {
    const std::scoped_lock lock(mutex);
    if (command.type == ControlCommandType::QuerySessionState) {
      return session_state(command, engine_preset);
    }
    if (command.type == ControlCommandType::LoadPreset &&
        reject_next_history_load) {
      reject_next_history_load = false;
      return rejected(command);
    }
    if (sar::control::control_command_mutates_preset(command.type)) {
      if (reject_next_mutation) {
        reject_next_mutation = false;
        return rejected(command);
      }
      auto result = sar::control::apply_command(engine_preset, command);
      assert(result.ok());
      engine_preset = result.take_document();
    }
    return accepted(command);
  };

  sar::gui::EngineController controller(transport, false);
  controller.refresh();
  assert(wait_until([&] {
    return !controller.busy() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 1.0);
  }));
  assert(!controller.canUndo());
  assert(!controller.canRedo());

  controller.setRouteGain(QStringLiteral("daw"), QStringLiteral("monitor"),
                          0.5);
  assert(wait_until([&] {
    return !controller.busy() && controller.canUndo() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 0.5);
  }));
  assert(!controller.canRedo());

  {
    const std::scoped_lock lock(mutex);
    reject_next_history_load = true;
  }
  controller.undo();
  assert(wait_until([&] { return !controller.busy(); }));
  assert(controller.canUndo());
  assert(!controller.canRedo());
  assert(near(controller.routeGain(QStringLiteral("daw"),
                                  QStringLiteral("monitor")), 0.5));

  controller.undo();
  assert(wait_until([&] {
    return !controller.busy() && !controller.canUndo() &&
           controller.canRedo() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 1.0);
  }));
  controller.redo();
  assert(wait_until([&] {
    return !controller.busy() && controller.canUndo() &&
           !controller.canRedo() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 0.5);
  }));

  controller.undo();
  assert(wait_until([&] {
    return !controller.busy() && controller.canRedo() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 1.0);
  }));
  controller.setRoute(QStringLiteral("daw"), QStringLiteral("monitor"),
                      false);
  assert(wait_until([&] {
    return !controller.busy() && controller.canUndo() &&
           !controller.canRedo() &&
           !controller.routeEnabled(QStringLiteral("daw"),
                                    QStringLiteral("monitor"));
  }));

  const auto preset_root = QStandardPaths::writableLocation(
                               QStandardPaths::AppDataLocation) +
                           QStringLiteral("/presets");
  sar::gui::PresetStore store(preset_root);
  QString error;
  assert(store.save(QStringLiteral("history-reset"), preset_with_gain(0.75F),
                    &error));
  controller.loadPreset(QStringLiteral("history-reset"));
  assert(wait_until([&] {
    return !controller.busy() && !controller.canUndo() &&
           !controller.canRedo() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 0.75);
  }));

  {
    const std::scoped_lock lock(mutex);
    reject_next_mutation = true;
  }
  controller.setRouteGain(QStringLiteral("daw"), QStringLiteral("monitor"),
                          0.9);
  assert(wait_until([&] { return !controller.busy(); }));
  assert(!controller.canUndo());
  assert(!controller.canRedo());
  assert(near(controller.routeGain(QStringLiteral("daw"),
                                  QStringLiteral("monitor")), 0.75));

  for (int index = 1; index <= 65; ++index) {
    const auto gain = static_cast<double>(index) / 100.0;
    controller.setRouteGain(QStringLiteral("daw"),
                            QStringLiteral("monitor"), gain);
    assert(wait_until([&] {
      return !controller.busy() &&
             near(controller.routeGain(QStringLiteral("daw"),
                                       QStringLiteral("monitor")), gain);
    }));
  }

  int undo_count = 0;
  while (controller.canUndo()) {
    controller.undo();
    assert(wait_until([&] { return !controller.busy(); }));
    ++undo_count;
  }
  assert(undo_count == 64);
  controller.refresh();
  assert(wait_until([&] {
    return !controller.busy() &&
           near(controller.routeGain(QStringLiteral("daw"),
                                     QStringLiteral("monitor")), 0.01);
  }));
  assert(controller.canRedo());
  return 0;
}
