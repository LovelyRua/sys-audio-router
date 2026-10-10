#include "app/gui_vstgui/engine_client.h"
#include "core/control/control_wire_protocol.h"
#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>
#include <atomic>
#include <cassert>
#include <vector>

int main() {
  using namespace sar;
  service::NamedPipeControlConfig config;
  config.pipe_name = L"sar-vstgui-client-test-" + std::to_wstring(GetCurrentProcessId());
  std::atomic_bool reject_session{false};
  std::atomic_uint diagnostics_queries{0};
  std::atomic_uint route_changes{0};
  std::atomic_uint runtime_configurations{0};
  control::AudioRuntimeConfiguration runtime_configuration;
  bool connected = false;
  float gain = 1.0F;
  bool muted = false;
  service::WindowsNamedPipeControlServer server(config,
      [&](std::span<const std::byte> payload) {
        std::vector<std::uint8_t> bytes;
        for (auto value : payload) bytes.push_back(std::to_integer<std::uint8_t>(value));
        const auto decoded = control::decode_control_command(bytes);
        assert(decoded.ok());
        const auto& command = decoded.command;
        auto response = control::command_accepted(command.command_id);
        switch (command.type) {
          case control::ControlCommandType::StartAudioRuntime:
          case control::ControlCommandType::StopAudioRuntime:
            response = control::command_rejected(command.command_id,
                {{"test_failure", "Device could not start or stop"}});
            break;
          case control::ControlCommandType::QueryAudioRuntime:
            response.has_audio_runtime_state = true;
            response.audio_runtime.configured =
                runtime_configuration.mode != control::AudioRuntimeMode::None;
            response.audio_runtime.configuration = runtime_configuration;
            break;
          case control::ControlCommandType::QuerySessionState:
            if (reject_session.load()) {
              response = control::command_rejected(command.command_id,
                  {{"session_failure", "Session query failed"}});
            } else {
              response.has_active_graph = true;
              response.active_graph.sample_rate = 48000;
              response.active_graph.frames = 128;
              response.has_preset = true;
              response.preset.matrix.inputs = {{"mic", "Microphone"}};
              response.preset.matrix.outputs = {{"main", "Main out"}};
              response.has_devices = true;
              response.devices = {
                  {"capture-device", "Capture device", platform::AudioBackendKind::Wasapi,
                   platform::AudioDeviceDirection::Input, {}, false, false, 2, 0},
                  {"render-device", "Render device", platform::AudioBackendKind::Wasapi,
                   platform::AudioDeviceDirection::Output, {}, true, false, 0, 2},
              };
              if (connected) response.preset.matrix.routes = {{"mic", "main", gain, muted}};
            }
            break;
          case control::ControlCommandType::ConfigureAudioRuntime:
            assert(command.audio_runtime.mode == control::AudioRuntimeMode::WasapiMatrix);
            assert(command.audio_runtime.endpoints.size() == 2);
            runtime_configuration = command.audio_runtime;
            ++runtime_configurations;
            break;
          case control::ControlCommandType::ConnectRoute:
          case control::ControlCommandType::DisconnectRoute:
            assert(command.input_id == "mic" && command.output_id == "main");
            assert(command.type != control::ControlCommandType::ConnectRoute || command.gain == 1.0F);
            connected = command.type == control::ControlCommandType::ConnectRoute;
            ++route_changes;
            break;
          case control::ControlCommandType::SetGain:
            assert(command.input_id == "mic" && command.output_id == "main");
            gain = command.gain;
            break;
          case control::ControlCommandType::SetMute:
            assert(command.input_id == "mic" && command.output_id == "main");
            muted = command.mute;
            break;
          case control::ControlCommandType::QueryDiagnostics:
            ++diagnostics_queries;
            response.has_diagnostics = true;
            break;
          default: assert(false);
        }
        const auto encoded = control::encode_control_response(response);
        assert(encoded.ok());
        std::vector<std::byte> result;
        for (auto value : encoded.bytes) result.push_back(static_cast<std::byte>(value));
        return service::NamedPipeControlResult::success(std::move(result));
      });
  assert(server.start().ok());
  gui_vstgui::EngineClient client(config.pipe_name);
  const auto initial = client.poll();
  assert(initial.transportOk && !initial.runtimeConfigured);
  assert(initial.sampleRate == 48000 && initial.blockFrames == 128);
  assert(initial.hasMatrix && initial.matrix.inputs.size() == 1);
  assert(initial.matrix.outputs.front().label == "Main out");
  assert(initial.devices.size() == 2);
  assert(initial.audioRuntimeConfiguration.mode == control::AudioRuntimeMode::None);
  assert(initial.matrix.routes.empty());
  const auto routed = client.setRoute("mic", "main", true);
  assert(routed.hasMatrix && routed.matrix.routes.size() == 1);
  const auto quieter = client.setRouteGain("mic", "main", 0.5F);
  assert(quieter.matrix.routes.size() == 1 &&
         quieter.matrix.routes.front().gain == 0.5F);
  const auto silent = client.setRouteMuted("mic", "main", true);
  assert(silent.matrix.routes.size() == 1 && silent.matrix.routes.front().muted);
  const auto disconnected = client.setRoute("mic", "main", false);
  assert(disconnected.matrix.routes.empty() && route_changes.load() == 2);
  control::AudioRuntimeConfiguration configuration;
  configuration.mode = control::AudioRuntimeMode::WasapiMatrix;
  configuration.endpoints = {
      {"capture-1", "capture-device", control::AudioRuntimeEndpointDirection::Capture,
       false, 1, 1},
      {"render-1", "render-device", control::AudioRuntimeEndpointDirection::Render,
       true, 0, 2},
  };
  const auto configured = client.configureAudioRuntime(configuration);
  assert(configured.transportOk && configured.runtimeConfigured);
  assert(configured.audioRuntimeConfiguration.mode ==
         control::AudioRuntimeMode::WasapiMatrix);
  assert(configured.audioRuntimeConfiguration.endpoints.size() == 2);
  assert(configured.audioRuntimeConfiguration.endpoints.front().first_channel == 1);
  assert(configured.audioRuntimeConfiguration.endpoints.front().channel_count == 1);
  assert(runtime_configurations.load() == 1);
  assert(initial.lastError.empty());
  assert(client.start().lastError == "Device could not start or stop");
  assert(client.stop().lastError == "Device could not start or stop");
  const auto count = diagnostics_queries.load();
  reject_session = true;
  assert(client.poll().lastError == "Session query failed");
  assert(diagnostics_queries.load() == count);
  server.stop();
  const auto offline = client.poll();
  assert(!offline.transportOk && !offline.lastError.empty());
}
