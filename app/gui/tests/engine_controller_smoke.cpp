#include "app/gui/engine_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <cassert>
#include <mutex>
#include <vector>

namespace {

using sar::control::AudioRuntimeMode;
using sar::control::ControlCommand;
using sar::control::ControlCommandType;
using sar::gui::EngineReply;

EngineReply accepted(const ControlCommand &command) {
  EngineReply reply;
  reply.request_type = command.type;
  reply.transport_ok = true;
  reply.response.command_id = command.command_id;
  return reply;
}

EngineReply uncertain(const ControlCommand &command) {
  EngineReply reply;
  reply.request_type = command.type;
  reply.delivery_uncertain = true;
  reply.error = QStringLiteral(
      "The engine response timed out; confirming the current state");
  return reply;
}

EngineReply confirmed_state(const ControlCommand &command) {
  auto reply = accepted(command);
  reply.response.has_session_state = true;
  reply.response.has_preset = true;
  reply.response.preset.sample_rate = 48'000;
  reply.response.preset.frames_per_block = 128;
  reply.response.preset.matrix.inputs = {{"daw", "DAW"}};
  reply.response.preset.matrix.outputs = {{"monitor", "Monitor"}};
  reply.response.preset.matrix.routes = {{"daw", "monitor", 1.0F, false}};
  reply.response.has_audio_runtime_state = true;
  reply.response.audio_runtime.installed = true;
  reply.response.audio_runtime.configured = true;
  reply.response.audio_runtime.running = true;
  reply.response.audio_runtime.configuration.mode =
      AudioRuntimeMode::WasapiDuplex;
  reply.response.audio_runtime.configuration.capture_device_id = "capture-1";
  reply.response.audio_runtime.configuration.render_device_id = "render-1";
  return reply;
}

bool wait_until(const std::function<bool()> &predicate, int timeout_ms = 3000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return predicate();
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);

  std::mutex mutex;
  std::vector<ControlCommand> requests;
  const auto transport = [&](ControlCommand command) {
    {
      const std::scoped_lock lock(mutex);
      requests.push_back(command);
    }
    if (command.type == ControlCommandType::ConfigureAudioRuntime ||
        command.type == ControlCommandType::ConnectRoute) {
      return uncertain(command);
    }
    assert(command.type == ControlCommandType::QuerySessionState);
    return confirmed_state(command);
  };

  sar::gui::EngineController controller(transport, false);
  controller.configureAudioRuntime(QStringLiteral("duplex"),
                                   QStringLiteral("capture-1"),
                                   QStringLiteral("render-1"));

  assert(wait_until(
      [&] { return !controller.busy() && controller.runtimeConfigured(); }));
  assert(controller.connected());
  assert(controller.runtimeRunning());
  assert(controller.runtimeMode() == QStringLiteral("duplex"));
  assert(controller.runtimeCaptureDeviceId() == QStringLiteral("capture-1"));
  assert(controller.runtimeRenderDeviceId() == QStringLiteral("render-1"));
  assert(controller.lastError().contains(QStringLiteral("timed out")));

  {
    const std::scoped_lock lock(mutex);
    assert(requests.size() == 2);
    assert(requests[0].type == ControlCommandType::ConfigureAudioRuntime);
    assert(requests[0].audio_runtime.mode == AudioRuntimeMode::WasapiDuplex);
    assert(requests[0].audio_runtime.capture_device_id == "capture-1");
    assert(requests[0].audio_runtime.render_device_id == "render-1");
    assert(requests[1].type == ControlCommandType::QuerySessionState);
    assert(requests[0].command_id != requests[1].command_id);
  }

  controller.setRoute(QStringLiteral("daw"), QStringLiteral("monitor"), true);
  assert(wait_until([&] {
    std::scoped_lock lock(mutex);
    return requests.size() == 4 && !controller.busy();
  }));
  assert(controller.routeEnabled(QStringLiteral("daw"),
                                 QStringLiteral("monitor")));
  assert(controller.runtimeMode() == QStringLiteral("duplex"));
  assert(controller.runtimeCaptureDeviceId() == QStringLiteral("capture-1"));
  assert(controller.runtimeRenderDeviceId() == QStringLiteral("render-1"));

  {
    const std::scoped_lock lock(mutex);
    assert(requests[2].type == ControlCommandType::ConnectRoute);
    assert(requests[3].type == ControlCommandType::QuerySessionState);
  }
  return 0;
}
