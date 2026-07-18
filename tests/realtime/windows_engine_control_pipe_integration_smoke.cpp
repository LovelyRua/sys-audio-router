#include "core/control/control_wire_protocol.h"
#include "core/platform/mock_audio_device_provider.h"
#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 64;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input", "Input"});
  preset.matrix.outputs.push_back({"output", "Output"});
  preset.matrix.routes.push_back({"input", "output", 1.0F, false});
  return preset;
}

std::vector<std::byte> as_bytes(std::span<const std::uint8_t> input) {
  std::vector<std::byte> result(input.size());
  std::transform(input.begin(), input.end(), result.begin(), [](std::uint8_t value) {
    return static_cast<std::byte>(value);
  });
  return result;
}

std::vector<std::uint8_t> as_u8(std::span<const std::byte> input) {
  std::vector<std::uint8_t> result(input.size());
  std::transform(input.begin(), input.end(), result.begin(), [](std::byte value) {
    return std::to_integer<std::uint8_t>(value);
  });
  return result;
}

class FakeAudioRuntime final : public sar::service::EngineAudioRuntime {
 public:
  explicit FakeAudioRuntime(std::uint64_t graph_version)
      : graph_version_(graph_version) {}

  sar::service::EngineAudioRuntimeResult start(std::uint32_t) override {
    running_ = true;
    return sar::service::EngineAudioRuntimeResult::success();
  }

  void stop() noexcept override { running_ = false; }
  bool running() const noexcept override { return running_; }
  std::uint64_t graph_version() const noexcept override { return graph_version_; }

  sar::diagnostics::EngineDiagnostics diagnostics() const override {
    sar::diagnostics::EngineDiagnostics result;
    result.graph_version = graph_version_;
    result.processed_blocks = 64;
    return result;
  }

 private:
  bool running_ = false;
  std::uint64_t graph_version_ = 0;
};

sar::control::ControlResponse send(
    const sar::service::NamedPipeControlConfig& config,
    const sar::control::ControlCommand& command) {
  const auto encoded = sar::control::encode_control_command(command);
  assert(encoded.ok());
  const auto transaction =
      sar::service::transact_named_pipe_control(config, as_bytes(encoded.bytes));
  assert(transaction.ok());
  const auto decoded =
      sar::control::decode_control_response(as_u8(transaction.payload()));
  assert(decoded.ok());
  assert(decoded.response.command_id == command.command_id);
  return decoded.response;
}

}  // namespace

int main() {
  auto created = sar::service::EngineControlService::create(make_preset(), 50);
  assert(created.ok());
  auto service = created.take_service();

  sar::platform::AudioDeviceDescriptor render_device;
  render_device.id = "render-1";
  render_device.label = "Render 1";
  render_device.backend = sar::platform::AudioBackendKind::Wasapi;
  render_device.direction = sar::platform::AudioDeviceDirection::Output;
  render_device.formats.push_back({
      48000,
      2,
      128,
      32,
      32,
      sar::platform::AudioSampleFormat::IeeeFloat,
  });
  render_device.is_default = true;
  service->add_audio_device_provider(
      std::make_unique<sar::platform::MockAudioDeviceProvider>(
          std::vector<sar::platform::AudioDeviceDescriptor>{render_device}));
  service->set_audio_runtime_configurator(
      [](const sar::control::AudioRuntimeConfiguration&,
         std::shared_ptr<sar::graph::Graph> graph) {
        return sar::service::EngineAudioRuntimeBuildResult::success(
            std::make_unique<FakeAudioRuntime>(graph->version()));
      });

  sar::service::NamedPipeControlConfig config;
  config.pipe_name = L"sys-audio-route-engine-integration-" +
                     std::to_wstring(GetCurrentProcessId());
  sar::service::WindowsNamedPipeControlServer server(
      config,
      [&service](std::span<const std::byte> request) {
        const auto encoded = service->handle_wire_request(as_u8(request));
        if (!encoded.ok()) {
          return sar::service::NamedPipeControlResult::failure({
              "control_response_encode_failed",
              "Engine control response could not be encoded.",
              0,
          });
        }
        return sar::service::NamedPipeControlResult::success(
            as_bytes(encoded.bytes));
      });
  assert(server.start().ok());

  sar::control::ControlCommand devices;
  devices.command_id = "devices";
  devices.type = sar::control::ControlCommandType::ListDevices;
  const auto listed = send(config, devices);
  assert(listed.status == sar::control::ControlResponseStatus::Accepted);
  assert(listed.has_devices);
  assert(listed.devices.size() == 1);
  assert(listed.devices[0].id == "render-1");

  sar::control::ControlCommand state;
  state.command_id = "state";
  state.type = sar::control::ControlCommandType::QuerySessionState;
  const auto session = send(config, state);
  assert(session.has_session_state);
  assert(session.active_graph.version == 50);
  assert(session.devices.size() == 1);

  sar::control::ControlCommand configure;
  configure.command_id = "configure";
  configure.type = sar::control::ControlCommandType::ConfigureAudioRuntime;
  configure.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiRender;
  configure.audio_runtime.render_device_id = "render-1";
  const auto configured = send(config, configure);
  assert(configured.has_audio_runtime_state);
  assert(configured.audio_runtime.installed);
  assert(!configured.audio_runtime.running);

  sar::control::ControlCommand start;
  start.command_id = "start";
  start.type = sar::control::ControlCommandType::StartAudioRuntime;
  const auto started = send(config, start);
  assert(started.audio_runtime.running);

  sar::control::ControlCommand diagnostics;
  diagnostics.command_id = "diagnostics";
  diagnostics.type = sar::control::ControlCommandType::QueryDiagnostics;
  const auto measured = send(config, diagnostics);
  assert(measured.has_diagnostics);
  assert(measured.diagnostics.processed_blocks == 64);

  sar::control::ControlCommand stop;
  stop.command_id = "stop";
  stop.type = sar::control::ControlCommandType::StopAudioRuntime;
  const auto stopped = send(config, stop);
  assert(stopped.audio_runtime.installed);
  assert(!stopped.audio_runtime.running);

  server.stop();
  const auto stats = server.stats();
  assert(stats.completed_requests == 6);
  assert(stats.protocol_errors == 0);
  assert(stats.handler_errors == 0);
}
