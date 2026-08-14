#include "app/gui/engine_controller.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QThread>

#include <algorithm>
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
  if (command.type == ControlCommandType::QueryVirtualAsioDevices) {
    reply.response.has_virtual_asio_devices = true;
    reply.response.virtual_asio_devices = {{
        .device_id = "default",
        .clsid = "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}",
        .registry_name = "System Audio Route",
        .broker_token = "virtual-asio",
        .input_channels = 2,
        .output_channels = 2,
        .enabled = true,
    }};
    return reply;
  }
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
  sar::control::AudioEndpointRuntimeDiagnostics endpoint;
  endpoint.endpoint_id = "capture-a";
  endpoint.role = sar::control::AudioEndpointRuntimeRole::Follower;
  endpoint.diagnostics.xrun_count = 2;
  endpoint.diagnostics.render_fifo_underflow_frames = 256;
  endpoint.recovery = reply.response.wasapi_recovery;
  endpoint.queue_fill_frames = 384;
  endpoint.correction_ppm = -17.25;
  reply.response.endpoint_diagnostics.push_back(std::move(endpoint));
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
        command.type == ControlCommandType::SetMute ||
        command.type == ControlCommandType::DisconnectRoute) {
      return uncertain(command);
    }
    assert(command.type == ControlCommandType::QuerySessionState ||
           command.type == ControlCommandType::QueryVirtualAsioDevices);
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
  const auto endpoint_diagnostics = controller.endpointDiagnostics();
  assert(endpoint_diagnostics.size() == 1);
  const auto endpoint = endpoint_diagnostics[0].toMap();
  assert(endpoint.value(QStringLiteral("endpointId")).toString() ==
         QStringLiteral("capture-a"));
  assert(endpoint.value(QStringLiteral("role")).toString() ==
         QStringLiteral("FOLLOWER"));
  assert(endpoint.value(QStringLiteral("health")).toString() ==
         QStringLiteral("Degraded"));
  assert(endpoint.value(QStringLiteral("queueFillFrames")).toULongLong() ==
         384);
  assert(endpoint.value(QStringLiteral("correctionPpm")).toDouble() ==
         -17.25);
  assert(endpoint.value(QStringLiteral("xruns")).toULongLong() == 2);
  assert(controller.lastError().contains(QStringLiteral("timed out")));

  {
    const std::scoped_lock lock(mutex);
    assert(requests.size() == 3);
    assert(requests[0].type == ControlCommandType::ConfigureAudioRuntime);
    assert(requests[0].audio_runtime.mode == AudioRuntimeMode::WasapiDuplex);
    assert(requests[0].audio_runtime.capture_device_id == "capture-1");
    assert(requests[0].audio_runtime.render_device_id == "render-1");
    assert(requests[1].type == ControlCommandType::QuerySessionState);
    assert(requests[2].type == ControlCommandType::QueryVirtualAsioDevices);
    assert(requests[0].command_id != requests[1].command_id);
    assert(requests[1].command_id != requests[2].command_id);
  }

  controller.setRoute(QStringLiteral("daw"), QStringLiteral("monitor"), false);
  assert(wait_until([&] {
    std::scoped_lock lock(mutex);
    return requests.size() == 6 && !controller.busy();
  }));
  assert(controller.routeEnabled(QStringLiteral("daw"),
                                 QStringLiteral("monitor")));
  assert(controller.runtimeMode() == QStringLiteral("duplex"));
  assert(controller.runtimeCaptureDeviceId() == QStringLiteral("capture-1"));
  assert(controller.runtimeRenderDeviceId() == QStringLiteral("render-1"));

  {
    const std::scoped_lock lock(mutex);
    assert(requests[3].type == ControlCommandType::SetMute);
    assert(requests[3].mute);
    assert(requests[4].type == ControlCommandType::QuerySessionState);
    assert(requests[5].type == ControlCommandType::QueryVirtualAsioDevices);
  }

  controller.removeRoute(QStringLiteral("daw"), QStringLiteral("monitor"));
  assert(wait_until([&] {
    std::scoped_lock lock(mutex);
    return requests.size() == 9 && !controller.busy();
  }));
  {
    const std::scoped_lock lock(mutex);
    assert(requests[6].type == ControlCommandType::DisconnectRoute);
    assert(requests[7].type == ControlCommandType::QuerySessionState);
    assert(requests[8].type == ControlCommandType::QueryVirtualAsioDevices);
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
    if (command.type == ControlCommandType::QueryVirtualAsioDevices)
      return confirmed_state(command);
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
    if (command.type == ControlCommandType::QueryVirtualAsioDevices)
      return confirmed_state(command);
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
    if (command.type == ControlCommandType::QueryVirtualAsioDevices)
      return confirmed_state(command);
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
    if (command.type == ControlCommandType::QueryVirtualAsioDevices) {
      return confirmed_state(command);
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
    assert(muted_requests.size() == 5);
    assert(muted_requests[0].type == ControlCommandType::QuerySessionState);
    assert(muted_requests[1].type == ControlCommandType::QueryVirtualAsioDevices);
    assert(muted_requests[2].type == ControlCommandType::SetMute);
    assert(muted_requests[3].type == ControlCommandType::QuerySessionState);
    assert(muted_requests[4].type == ControlCommandType::QueryVirtualAsioDevices);
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

  ControlCommand matrix_request;
  const auto matrix_transport = [&](ControlCommand command) {
    auto reply = accepted(command);
    if (command.type == ControlCommandType::ConfigureAudioRuntime) {
      matrix_request = command;
      reply.response.has_audio_runtime_state = true;
      reply.response.audio_runtime.configured = true;
      reply.response.audio_runtime.configuration = command.audio_runtime;
    } else if (command.type == ControlCommandType::QuerySessionState) {
      assert(command.type == ControlCommandType::QuerySessionState);
    } else {
      assert(command.type == ControlCommandType::QueryVirtualAsioDevices);
      return confirmed_state(command);
    }
    return reply;
  };
  sar::gui::EngineController matrix_controller(matrix_transport, false);
  matrix_controller.configureAudioMatrix({
      QVariantMap{{QStringLiteral("endpointId"), QStringLiteral("capture-a")},
                  {QStringLiteral("deviceId"), QStringLiteral("capture-native")},
                  {QStringLiteral("direction"), QStringLiteral("capture")},
                  {QStringLiteral("clockMaster"), false},
                  {QStringLiteral("firstChannel"), 0},
                  {QStringLiteral("channelCount"), 2}},
      QVariantMap{{QStringLiteral("endpointId"), QStringLiteral("render-main")},
                  {QStringLiteral("deviceId"), QStringLiteral("render-native")},
                  {QStringLiteral("direction"), QStringLiteral("render")},
                  {QStringLiteral("clockMaster"), true},
                  {QStringLiteral("firstChannel"), 0},
                  {QStringLiteral("channelCount"), 2}},
  });
  assert(wait_until([&] {
    return !matrix_controller.busy() &&
           matrix_request.type == ControlCommandType::ConfigureAudioRuntime;
  }));
  assert(matrix_request.audio_runtime.mode == AudioRuntimeMode::WasapiMatrix);
  assert(matrix_request.audio_runtime.endpoints.size() == 2);
  assert(matrix_request.audio_runtime.endpoints[1].clock_master);
  assert(matrix_controller.runtimeMode() == QStringLiteral("matrix"));
  assert(matrix_controller.runtimeEndpoints().size() == 2);
  assert(matrix_controller.runtimeEndpoints()[1].toMap()
             .value(QStringLiteral("endpointId")) ==
         QStringLiteral("render-main"));

  ControlCommand topology_request;
  std::vector<sar::control::VirtualAsioDeviceDefinition> topology_devices = {{
      .device_id = "default",
      .clsid = "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}",
      .registry_name = "System Audio Route",
      .broker_token = "virtual-asio",
      .input_channels = 2,
      .output_channels = 2,
      .enabled = true,
  }};
  const auto topology_transport = [&](ControlCommand command) {
    if (command.type == ControlCommandType::ConfigureVirtualAsioDevices) {
      topology_request = command;
      topology_devices = command.virtual_asio_devices;
      return accepted(command);
    }
    if (command.type == ControlCommandType::QueryVirtualAsioDevices) {
      auto reply = accepted(command);
      reply.response.has_virtual_asio_devices = true;
      reply.response.virtual_asio_devices = topology_devices;
      return reply;
    }
    return confirmed_state(command);
  };
  sar::gui::EngineController topology_controller(topology_transport, false);
  int topology_applied_count = 0;
  int topology_refreshed_count = 0;
  QObject::connect(&topology_controller,
                   &sar::gui::EngineController::virtualAsioTopologyApplied,
                   [&] { ++topology_applied_count; });
  QObject::connect(&topology_controller,
                   &sar::gui::EngineController::virtualAsioDevicesRefreshed,
                   [&] { ++topology_refreshed_count; });
  topology_controller.refresh();
  assert(wait_until([&] {
    return !topology_controller.busy() &&
           topology_controller.virtualAsioDevices().size() == 1;
  }));
  const auto initial_device =
      topology_controller.virtualAsioDevices().front().toMap();
  assert(initial_device.value(QStringLiteral("registryName")).toString() ==
         QStringLiteral("System Audio Route"));
  assert(topology_refreshed_count == 1);
  topology_controller.configureVirtualAsioDevices({
      QVariantMap{{QStringLiteral("deviceId"), QString{}},
                  {QStringLiteral("clsid"), QString{}},
                  {QStringLiteral("registryName"), QStringLiteral("Studio A")},
                  {QStringLiteral("brokerToken"), QString{}},
                  {QStringLiteral("inputChannels"), 8},
                  {QStringLiteral("outputChannels"), 8},
                  {QStringLiteral("enabled"), true}},
  });
  assert(wait_until([&] {
    return !topology_controller.busy() &&
           topology_request.type ==
               ControlCommandType::ConfigureVirtualAsioDevices &&
           topology_refreshed_count == 2;
  }));
  assert(topology_applied_count == 1);
  assert(topology_request.virtual_asio_devices.size() == 1);
  const auto &generated = topology_request.virtual_asio_devices.front();
  assert(!generated.device_id.empty());
  assert(generated.clsid.size() == 38);
  assert(!generated.broker_token.empty());
  assert(generated.registry_name == "Studio A");
  assert(generated.input_channels == 8);
  assert(generated.output_channels == 8);
  assert(generated.enabled);
  assert(topology_controller.virtualAsioDevices().front().toMap()
             .value(QStringLiteral("deviceId"))
             .toString() == QString::fromStdString(generated.device_id));

  topology_request = {};
  topology_controller.configureVirtualAsioDevices({
      QVariantMap{{QStringLiteral("registryName"), QString{}},
                  {QStringLiteral("inputChannels"), 2},
                  {QStringLiteral("outputChannels"), 2},
                  {QStringLiteral("enabled"), true}},
  });
  assert(topology_request.type !=
         ControlCommandType::ConfigureVirtualAsioDevices);
  assert(topology_controller.lastError().contains(QStringLiteral("name")));

  int empty_topology_refreshes = 0;
  const auto empty_topology_transport = [&](ControlCommand command) {
    if (command.type == ControlCommandType::QueryVirtualAsioDevices)
      return accepted(command);
    return confirmed_state(command);
  };
  sar::gui::EngineController empty_topology_controller(
      empty_topology_transport, false);
  QObject::connect(
      &empty_topology_controller,
      &sar::gui::EngineController::virtualAsioDevicesRefreshed,
      [&] { ++empty_topology_refreshes; });
  empty_topology_controller.refresh();
  assert(wait_until([&] { return !empty_topology_controller.busy(); }));
  assert(empty_topology_refreshes == 0);

  std::mutex poll_mutex;
  std::vector<ControlCommandType> poll_requests;
  const auto poll_transport = [&](ControlCommand command) {
    {
      const std::scoped_lock lock(poll_mutex);
      poll_requests.push_back(command.type);
    }
    if (command.type == ControlCommandType::QuerySessionState)
      return confirmed_state(command);
    return accepted(command);
  };
  sar::gui::EngineController poll_controller(poll_transport, false);
  QElapsedTimer poll_timer;
  poll_timer.start();
  while (poll_timer.elapsed() < 5000) {
    {
      const std::scoped_lock lock(poll_mutex);
      if (poll_requests.size() >= 100)
        break;
    }
    assert(QMetaObject::invokeMethod(&poll_controller, "schedulePoll",
                                     Qt::DirectConnection));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  assert(wait_until([&] {
    const std::scoped_lock lock(poll_mutex);
    return poll_requests.size() >= 100;
  }));
  {
    const std::scoped_lock lock(poll_mutex);
    assert(std::ranges::count(poll_requests,
                              ControlCommandType::QuerySessionState) == 1);
    assert(std::ranges::count(
               poll_requests,
               ControlCommandType::QueryVirtualAsioDevices) == 0);
  }
  return 0;
}
