#include "app/gui/engine_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>

#include <array>
#include <atomic>
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

EngineReply disconnected(const ControlCommand &command) {
  EngineReply reply;
  reply.request_type = command.type;
  reply.error = QStringLiteral("The engine is offline");
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
  reply.response.has_diagnostics = true;
  reply.response.diagnostics.xrun_count = 3;
  reply.response.diagnostics.virtual_asio_producer_underflows = 17;
  reply.response.diagnostics.virtual_asio_producer_overflows = 5;
  reply.response.has_wasapi_recovery = true;
  reply.response.wasapi_recovery.state =
      sar::control::WasapiRecoveryState::Backoff;
  reply.response.wasapi_recovery.runtime_health =
      sar::control::WasapiRuntimeHealth::Degraded;
  reply.response.wasapi_recovery.runtime_reason_code =
      "capture_discontinuity";
  reply.response.wasapi_recovery.recovery_episode_count = 7;
  reply.response.wasapi_recovery.successful_recovery_count = 5;
  reply.response.wasapi_recovery.failed_recovery_count = 2;
  reply.response.wasapi_recovery.last_recovery_duration_ms = 84;
  reply.response.wasapi_recovery.maximum_recovery_duration_ms = 311;
  reply.response.wasapi_recovery.endpoint_notification_reopen_count = 4;
  reply.response.wasapi_recovery.endpoint_notification_reset_failure_count = 1;
  reply.response.wasapi_recovery.endpoint_notification_reopen_pending = true;
  reply.response.wasapi_recovery.wait_timeout_cycles = 13;
  reply.response.wasapi_recovery.capture_discontinuity_cycles = 8;
  reply.response.wasapi_recovery.render_fifo_underflow_frames = 1'024;
  reply.response.wasapi_recovery.maximum_render_recovery_silence_frames = 384;
  reply.response.wasapi_recovery
      .maximum_consecutive_capture_rate_clamped_frames = 2'048;
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
        command.type == ControlCommandType::DisconnectRoute) {
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
  assert(controller.xrunCount() == 3);
  assert(controller.virtualAsioProducerUnderflows() == 17);
  assert(controller.virtualAsioProducerOverflows() == 5);
  assert(controller.property("virtualAsioProducerUnderflows").toULongLong() ==
         17);
  assert(controller.property("virtualAsioProducerOverflows").toULongLong() ==
         5);
  assert(controller.wasapiRecoveryAvailable());
  assert(controller.wasapiRecoveryState() == QStringLiteral("Backoff"));
  assert(controller.wasapiRuntimeHealth() == QStringLiteral("Degraded"));
  assert(controller.wasapiRuntimeReasonCode() ==
         QStringLiteral("capture_discontinuity"));
  assert(controller.wasapiRecoveryEpisodes() == 7);
  assert(controller.wasapiSuccessfulRecoveries() == 5);
  assert(controller.wasapiFailedRecoveries() == 2);
  assert(controller.wasapiLastRecoveryMs() == 84);
  assert(controller.wasapiMaximumRecoveryMs() == 311);
  assert(controller.wasapiEndpointReopens() == 4);
  assert(controller.wasapiEndpointResetFailures() == 1);
  assert(controller.wasapiEndpointReopenPending());
  assert(controller.wasapiWaitTimeoutCycles() == 13);
  assert(controller.wasapiCaptureDiscontinuityCycles() == 8);
  assert(controller.wasapiRenderFifoUnderflowFrames() == 1'024);
  assert(controller.wasapiMaximumRenderRecoverySilenceFrames() == 384);
  assert(controller.wasapiMaximumConsecutiveCaptureRateClampedFrames() ==
         2'048);
  assert(controller.property("wasapiRuntimeHealth").toString() ==
         QStringLiteral("Degraded"));
  assert(controller.property("wasapiWaitTimeoutCycles").toULongLong() == 13);
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

  controller.setRoute(QStringLiteral("daw"), QStringLiteral("monitor"), false);
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
    assert(requests[2].type == ControlCommandType::DisconnectRoute);
    assert(requests[3].type == ControlCommandType::QuerySessionState);
  }

  const std::array recovery_states{
      std::pair{sar::control::WasapiRecoveryState::Stopped,
                QStringLiteral("Stopped")},
      std::pair{sar::control::WasapiRecoveryState::Opening,
                QStringLiteral("Opening")},
      std::pair{sar::control::WasapiRecoveryState::Running,
                QStringLiteral("Running")},
      std::pair{sar::control::WasapiRecoveryState::Quiescing,
                QStringLiteral("Quiescing")},
      std::pair{sar::control::WasapiRecoveryState::Backoff,
                QStringLiteral("Backoff")},
      std::pair{sar::control::WasapiRecoveryState::Faulted,
                QStringLiteral("Faulted")},
  };
  const std::array runtime_health_states{
      std::pair{sar::control::WasapiRuntimeHealth::Stopped,
                QStringLiteral("Stopped")},
      std::pair{sar::control::WasapiRuntimeHealth::Healthy,
                QStringLiteral("Healthy")},
      std::pair{sar::control::WasapiRuntimeHealth::Degraded,
                QStringLiteral("Degraded")},
      std::pair{sar::control::WasapiRuntimeHealth::Faulted,
                QStringLiteral("Faulted")},
  };
  std::atomic_size_t recovery_reply_index = 0;
  const auto recovery_transport = [&](ControlCommand command) {
    assert(command.type == ControlCommandType::QuerySessionState);
    auto reply = confirmed_state(command);
    const auto index = recovery_reply_index.fetch_add(1);
    if (index < recovery_states.size()) {
      reply.response.wasapi_recovery.state = recovery_states[index].first;
    } else {
      reply.response.has_wasapi_recovery = false;
    }
    return reply;
  };
  sar::gui::EngineController recovery_controller(recovery_transport, false);
  for (const auto &[state, label] : recovery_states) {
    (void)state;
    recovery_controller.refresh();
    assert(wait_until([&] {
      return !recovery_controller.busy() &&
             recovery_controller.wasapiRecoveryState() == label;
    }));
    assert(recovery_controller.wasapiRecoveryAvailable());
  }
  recovery_controller.refresh();
  assert(wait_until([&] {
    return !recovery_controller.busy() &&
           !recovery_controller.wasapiRecoveryAvailable();
  }));
  assert(recovery_controller.wasapiRecoveryState() ==
         QStringLiteral("Stopped"));
  assert(recovery_controller.wasapiRecoveryEpisodes() == 0);
  assert(recovery_controller.wasapiRuntimeHealth() ==
         QStringLiteral("Stopped"));
  assert(recovery_controller.wasapiRuntimeReasonCode().isEmpty());
  assert(recovery_controller.wasapiWaitTimeoutCycles() == 0);
  assert(recovery_controller.wasapiCaptureDiscontinuityCycles() == 0);
  assert(recovery_controller.wasapiRenderFifoUnderflowFrames() == 0);
  assert(recovery_controller.wasapiMaximumRenderRecoverySilenceFrames() == 0);
  assert(
      recovery_controller
          .wasapiMaximumConsecutiveCaptureRateClampedFrames() == 0);

  std::atomic_size_t health_reply_index = 0;
  const auto health_transport = [&](ControlCommand command) {
    assert(command.type == ControlCommandType::QuerySessionState);
    auto reply = confirmed_state(command);
    const auto index = health_reply_index.fetch_add(1);
    assert(index < runtime_health_states.size());
    reply.response.wasapi_recovery.runtime_health =
        runtime_health_states[index].first;
    return reply;
  };
  sar::gui::EngineController health_controller(health_transport, false);
  for (const auto &[health, label] : runtime_health_states) {
    (void)health;
    health_controller.refresh();
    assert(wait_until([&] {
      return !health_controller.busy() &&
             health_controller.wasapiRuntimeHealth() == label;
    }));
  }

  std::atomic_int reconnect_attempt = 0;
  const auto reconnect_transport = [&](ControlCommand command) {
    assert(command.type == ControlCommandType::QuerySessionState);
    if (reconnect_attempt.fetch_add(1) == 0) {
      return disconnected(command);
    }
    return confirmed_state(command);
  };
  sar::gui::EngineController reconnect_controller(reconnect_transport, false);
  reconnect_controller.refresh();
  assert(wait_until([&] { return !reconnect_controller.busy(); }));
  assert(!reconnect_controller.connected());
  assert(!reconnect_controller.lastError().isEmpty());
  reconnect_controller.refresh();
  assert(wait_until([&] {
    return !reconnect_controller.busy() && reconnect_controller.connected();
  }));
  assert(reconnect_controller.lastError().isEmpty());

  auto muted_state = confirmed_state(ControlCommand{});
  muted_state.response.preset.matrix.routes.front().muted = true;
  std::mutex muted_mutex;
  std::vector<ControlCommand> muted_requests;
  const auto muted_transport = [&](ControlCommand command) {
    const std::scoped_lock lock(muted_mutex);
    muted_requests.push_back(command);
    if (command.type == ControlCommandType::QuerySessionState) {
      auto state = muted_state;
      state.request_type = command.type;
      state.response.command_id = command.command_id;
      return state;
    }
    assert(command.type == ControlCommandType::SetMute);
    assert(!command.mute);
    muted_state.response.preset.matrix.routes.front().muted = false;
    return accepted(command);
  };
  sar::gui::EngineController muted_controller(muted_transport, false);
  muted_controller.refresh();
  assert(wait_until([&] { return !muted_controller.busy(); }));
  assert(!muted_controller.routeEnabled(QStringLiteral("daw"),
                                        QStringLiteral("monitor")));
  muted_controller.setRoute(QStringLiteral("daw"),
                            QStringLiteral("monitor"), true);
  assert(wait_until([&] {
    return !muted_controller.busy() &&
           muted_controller.routeEnabled(QStringLiteral("daw"),
                                         QStringLiteral("monitor"));
  }));
  {
    const std::scoped_lock lock(muted_mutex);
    assert(muted_requests.size() == 3);
    assert(muted_requests[0].type == ControlCommandType::QuerySessionState);
    assert(muted_requests[1].type == ControlCommandType::SetMute);
    assert(muted_requests[2].type == ControlCommandType::QuerySessionState);
  }

  std::string first_controller_command_id;
  std::string second_controller_command_id;
  const auto capture_first_id = [&](ControlCommand command) {
    first_controller_command_id = command.command_id;
    return confirmed_state(command);
  };
  const auto capture_second_id = [&](ControlCommand command) {
    second_controller_command_id = command.command_id;
    return confirmed_state(command);
  };
  sar::gui::EngineController first_controller(capture_first_id, false);
  sar::gui::EngineController second_controller(capture_second_id, false);
  first_controller.refresh();
  second_controller.refresh();
  assert(wait_until([&] {
    return !first_controller.busy() && !second_controller.busy() &&
           !first_controller_command_id.empty() &&
           !second_controller_command_id.empty();
  }));
  assert(first_controller_command_id != second_controller_command_id);
  return 0;
}
