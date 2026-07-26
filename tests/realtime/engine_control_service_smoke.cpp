#include "core/service/engine_control_service.h"
#include "core/platform/mock_audio_device_provider.h"

#include <cassert>
#include <memory>

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

class FakeAudioRuntime final : public sar::service::EngineAudioRuntime {
 public:
  explicit FakeAudioRuntime(std::uint64_t graph_version = 10)
      : graph_version_(graph_version) {}

  sar::service::EngineAudioRuntimeResult start(std::uint32_t) override {
    running_ = true;
    ++start_calls;
    return sar::service::EngineAudioRuntimeResult::success();
  }

  void stop() noexcept override {
    running_ = false;
    ++stop_calls;
  }

  bool running() const noexcept override { return running_; }

  std::uint64_t graph_version() const noexcept override { return graph_version_; }

  sar::diagnostics::EngineDiagnostics diagnostics() const override {
    sar::diagnostics::EngineDiagnostics result;
    result.graph_version = 10;
    result.processed_blocks = 42;
    result.xrun_count = 3;
    return result;
  }

  bool running_ = false;
  std::uint64_t graph_version_ = 10;
  std::uint32_t start_calls = 0;
  std::uint32_t stop_calls = 0;
};

sar::control::ControlResponse send(
    sar::service::EngineControlService& service,
    const sar::control::ControlCommand& command) {
  const auto request = sar::control::encode_control_command(command);
  assert(request.ok());
  const auto response_bytes = service.handle_wire_request(request.bytes);
  assert(response_bytes.ok());
  const auto response =
      sar::control::decode_control_response(response_bytes.bytes);
  assert(response.ok());
  return response.response;
}

}  // namespace

int main() {
  auto create = sar::service::EngineControlService::create(make_preset(), 10);
  assert(create.ok());
  auto service = create.take_service();

  sar::control::ControlCommand state;
  state.command_id = "state-1";
  state.type = sar::control::ControlCommandType::QuerySessionState;
  const auto before = send(*service, state);
  assert(before.status == sar::control::ControlResponseStatus::Accepted);
  assert(before.has_session_state);
  assert(before.next_graph_version == 11);
  assert(before.preset.matrix.routes[0].gain == 1.0F);

  const auto missing_runtime = service->start_audio_runtime();
  assert(!missing_runtime.ok());
  assert(missing_runtime.errors()[0].code == "audio_runtime_not_installed");

  sar::control::ControlCommand runtime_state;
  runtime_state.command_id = "runtime-state-1";
  runtime_state.type = sar::control::ControlCommandType::QueryAudioRuntime;
  const auto missing_runtime_state = send(*service, runtime_state);
  assert(missing_runtime_state.has_audio_runtime_state);
  assert(!missing_runtime_state.audio_runtime.installed);
  assert(!missing_runtime_state.audio_runtime.running);
  assert(missing_runtime_state.audio_runtime.graph_version == 0);

  sar::control::ControlCommand runtime_start;
  runtime_start.command_id = "runtime-start-1";
  runtime_start.type = sar::control::ControlCommandType::StartAudioRuntime;
  const auto missing_runtime_start = send(*service, runtime_start);
  assert(missing_runtime_start.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(missing_runtime_start.errors[0].code ==
         "audio_runtime_not_installed");

  auto fake_runtime = std::make_unique<FakeAudioRuntime>();
  auto* runtime_observer = fake_runtime.get();
  const auto installed =
      service->install_audio_runtime(std::move(fake_runtime));
  assert(installed.ok());
  assert(service->has_audio_runtime());
  assert(!service->audio_runtime_running());

  runtime_start.command_id = "runtime-start-2";
  const auto started = send(*service, runtime_start);
  assert(started.status == sar::control::ControlResponseStatus::Accepted);
  assert(started.has_audio_runtime_state);
  assert(started.audio_runtime.installed);
  assert(started.audio_runtime.running);
  assert(started.audio_runtime.graph_version == 10);
  assert(service->audio_runtime_running());
  assert(runtime_observer->start_calls == 1);

  runtime_start.command_id = "runtime-start-3";
  const auto duplicate_start = send(*service, runtime_start);
  assert(duplicate_start.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(duplicate_start.errors[0].code ==
         "audio_runtime_already_running");

  sar::control::ControlCommand diagnostics;
  diagnostics.command_id = "diagnostics-1";
  diagnostics.type = sar::control::ControlCommandType::QueryDiagnostics;
  const auto diagnostic_response = send(*service, diagnostics);
  assert(diagnostic_response.has_diagnostics);
  assert(diagnostic_response.diagnostics.graph_version == 10);
  assert(diagnostic_response.diagnostics.processed_blocks == 42);
  assert(diagnostic_response.diagnostics.xrun_count == 3);

  sar::control::ControlCommand gain;
  gain.command_id = "gain-1";
  gain.type = sar::control::ControlCommandType::SetGain;
  gain.input_id = "input";
  gain.output_id = "output";
  gain.gain = 0.25F;
  const auto blocked = send(*service, gain);
  assert(blocked.status == sar::control::ControlResponseStatus::Rejected);
  assert(blocked.errors[0].code ==
         "audio_runtime_graph_change_requires_restart");

  auto replacement = std::make_unique<FakeAudioRuntime>();
  const auto replace_while_running =
      service->install_audio_runtime(std::move(replacement));
  assert(!replace_while_running.ok());
  assert(replace_while_running.errors()[0].code == "audio_runtime_running");

  sar::control::ControlCommand runtime_stop;
  runtime_stop.command_id = "runtime-stop-1";
  runtime_stop.type = sar::control::ControlCommandType::StopAudioRuntime;
  const auto stopped = send(*service, runtime_stop);
  assert(stopped.status == sar::control::ControlResponseStatus::Accepted);
  assert(stopped.has_audio_runtime_state);
  assert(stopped.audio_runtime.installed);
  assert(!stopped.audio_runtime.running);
  assert(stopped.audio_runtime.graph_version == 10);
  assert(!service->audio_runtime_running());
  assert(runtime_observer->stop_calls == 1);

  const auto applied = send(*service, gain);
  assert(applied.status == sar::control::ControlResponseStatus::Accepted);

  runtime_start.command_id = "runtime-start-4";
  const auto stale_runtime = send(*service, runtime_start);
  assert(stale_runtime.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(stale_runtime.errors[0].code == "audio_runtime_graph_stale");

  state.command_id = "state-2";
  const auto after = send(*service, state);
  assert(after.next_graph_version == 12);
  assert(after.preset.matrix.routes[0].gain == 0.25F);

  auto observer_create =
      sar::service::EngineControlService::create(make_preset(), 15);
  assert(observer_create.ok());
  auto observer_service = observer_create.take_service();
  std::uint32_t observer_calls = 0;
  bool reject_observer = false;
  observer_service->set_preset_commit_observer(
      [&](const sar::control::PresetDocument& preset,
          std::uint64_t graph_version) {
        ++observer_calls;
        assert(graph_version >= 16);
        assert(preset.matrix.routes[0].gain == 0.5F ||
               preset.matrix.routes[0].gain == 0.25F);
        if (reject_observer) {
          return std::vector<sar::control::PresetError>{
              {"injected_preset_observer_failure",
               "Injected preset observer failure."},
          };
        }
        return std::vector<sar::control::PresetError>{};
      });
  gain.command_id = "observer-gain-accepted";
  gain.gain = 0.5F;
  const auto observer_accepted = send(*observer_service, gain);
  assert(observer_accepted.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(observer_calls == 1);
  assert(observer_service->session().current_preset().matrix.routes[0].gain ==
         0.5F);

  reject_observer = true;
  gain.command_id = "observer-gain-rejected";
  gain.gain = 0.25F;
  const auto observer_rejected = send(*observer_service, gain);
  assert(observer_rejected.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(observer_rejected.errors[0].code ==
         "injected_preset_observer_failure");
  assert(observer_calls == 2);
  assert(observer_service->session().current_preset().matrix.routes[0].gain ==
         0.5F);

  const std::uint8_t malformed[] = {0, 1, 2};
  const auto rejected_bytes = service->handle_wire_request(malformed);
  assert(rejected_bytes.ok());
  const auto rejected =
      sar::control::decode_control_response(rejected_bytes.bytes);
  assert(rejected.ok());
  assert(rejected.response.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(rejected.response.errors[0].code == "invalid_control_wire_request");

  auto rebuild_create =
      sar::service::EngineControlService::create(make_preset(), 20);
  assert(rebuild_create.ok());
  auto rebuild_service = rebuild_create.take_service();
  std::uint32_t rebuild_calls = 0;
  bool fail_rebuild = false;
  FakeAudioRuntime* rebuilt_observer = nullptr;
  sar::service::EngineAudioRuntimeBuilder builder =
      [&](std::shared_ptr<sar::graph::Graph> graph) {
        ++rebuild_calls;
        if (fail_rebuild) {
          return sar::service::EngineAudioRuntimeBuildResult::failure({
              {"fake_runtime_rebuild_failed", "Injected runtime rebuild failure."},
          });
        }
        auto runtime = std::make_unique<FakeAudioRuntime>(graph->version());
        rebuilt_observer = runtime.get();
        return sar::service::EngineAudioRuntimeBuildResult::success(
            std::move(runtime));
      };
  auto rebuild_runtime = std::make_unique<FakeAudioRuntime>(20);
  const auto rebuild_installed = rebuild_service->install_audio_runtime(
      std::move(rebuild_runtime), std::move(builder));
  assert(rebuild_installed.ok());
  const auto initial_start = rebuild_service->start_audio_runtime();
  assert(initial_start.ok());
  rebuild_service->stop_audio_runtime();

  gain.command_id = "rebuild-gain-1";
  gain.gain = 0.5F;
  const auto rebuild_graph = send(*rebuild_service, gain);
  assert(rebuild_graph.status == sar::control::ControlResponseStatus::Accepted);
  runtime_start.command_id = "runtime-rebuild-start-1";
  const auto rebuilt_start = send(*rebuild_service, runtime_start);
  assert(rebuilt_start.status == sar::control::ControlResponseStatus::Accepted);
  assert(rebuild_calls == 1);
  assert(rebuilt_observer != nullptr);
  assert(rebuilt_observer->running());
  assert(rebuilt_start.audio_runtime.graph_version == 21);
  assert(rebuilt_start.audio_runtime.running);

  rebuild_service->stop_audio_runtime();
  gain.command_id = "rebuild-gain-2";
  gain.gain = 0.75F;
  const auto second_graph = send(*rebuild_service, gain);
  assert(second_graph.status == sar::control::ControlResponseStatus::Accepted);
  fail_rebuild = true;
  runtime_start.command_id = "runtime-rebuild-start-2";
  const auto failed_rebuild = send(*rebuild_service, runtime_start);
  assert(failed_rebuild.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(failed_rebuild.errors[0].code == "fake_runtime_rebuild_failed");
  assert(rebuild_calls == 2);
  runtime_state.command_id = "runtime-state-after-rebuild-failure";
  const auto retained_runtime = send(*rebuild_service, runtime_state);
  assert(retained_runtime.audio_runtime.installed);
  assert(!retained_runtime.audio_runtime.running);
  assert(retained_runtime.audio_runtime.graph_version == 21);

  auto configure_create =
      sar::service::EngineControlService::create(make_preset(), 30);
  assert(configure_create.ok());
  auto configure_service = configure_create.take_service();
  sar::control::ControlCommand configure;
  configure.command_id = "configure-duplex-1";
  configure.type =
      sar::control::ControlCommandType::ConfigureAudioRuntime;
  configure.audio_runtime.mode =
      sar::control::AudioRuntimeMode::WasapiDuplex;
  configure.audio_runtime.capture_device_id = "capture-pinned";
  configure.audio_runtime.render_device_id = "render-pinned";
  const auto missing_configurator = send(*configure_service, configure);
  assert(missing_configurator.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(missing_configurator.errors[0].code ==
         "audio_runtime_configurator_not_installed");

  std::uint32_t configure_calls = 0;
  sar::control::AudioRuntimeConfiguration observed_configuration;
  configure_service->set_audio_runtime_configurator(
      [&](const sar::control::AudioRuntimeConfiguration& configuration,
          std::shared_ptr<sar::graph::Graph> graph) {
        ++configure_calls;
        observed_configuration = configuration;
        return sar::service::EngineAudioRuntimeBuildResult::success(
            std::make_unique<FakeAudioRuntime>(graph->version()));
      });
  const auto configured = send(*configure_service, configure);
  assert(configured.status == sar::control::ControlResponseStatus::Accepted);
  assert(configure_calls == 1);
  assert(configured.audio_runtime.installed);
  assert(!configured.audio_runtime.running);
  const auto configured_session = configure_service->session_document();
  assert(configured_session.preset.sample_rate == 48000);
  assert(configured_session.audio_runtime.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(configured_session.audio_runtime.capture_device_id ==
         "capture-pinned");
  assert(configured_session.audio_runtime.render_device_id ==
         "render-pinned");
  assert(!configured_session.auto_start);

  sar::control::ControlCommand start_configured;
  start_configured.command_id = "start-configured";
  start_configured.type =
      sar::control::ControlCommandType::StartAudioRuntime;
  const auto configured_started = send(*configure_service, start_configured);
  assert(configured_started.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(configure_service->session_document().auto_start);

  sar::control::ControlCommand stop_configured;
  stop_configured.command_id = "stop-configured";
  stop_configured.type = sar::control::ControlCommandType::StopAudioRuntime;
  const auto configured_stopped = send(*configure_service, stop_configured);
  assert(configured_stopped.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(!configure_service->session_document().auto_start);
  assert(configured.audio_runtime.configured);
  assert(configured.audio_runtime.configuration.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(configured.audio_runtime.configuration.capture_device_id ==
         "capture-pinned");
  assert(observed_configuration.render_device_id == "render-pinned");

  runtime_start.command_id = "configured-runtime-start";
  const auto configured_start = send(*configure_service, runtime_start);
  assert(configured_start.status ==
         sar::control::ControlResponseStatus::Accepted);
  configure.command_id = "configure-while-running";
  const auto configure_running = send(*configure_service, configure);
  assert(configure_running.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(configure_running.errors[0].code == "audio_runtime_running");

  configure_service->stop_audio_runtime();
  configure.command_id = "configure-render-default";
  configure.audio_runtime = {};
  configure.audio_runtime.mode =
      sar::control::AudioRuntimeMode::WasapiRender;
  const auto render_configured = send(*configure_service, configure);
  assert(render_configured.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(configure_calls == 2);
  assert(render_configured.audio_runtime.configuration.mode ==
         sar::control::AudioRuntimeMode::WasapiRender);
  assert(render_configured.audio_runtime.configuration.render_device_id.empty());

  auto devices_create =
      sar::service::EngineControlService::create(make_preset(), 40);
  assert(devices_create.ok());
  auto devices_service = devices_create.take_service();
  sar::platform::AudioDeviceDescriptor physical_device;
  physical_device.id = "wasapi-render-1";
  physical_device.label = "Physical Render";
  physical_device.backend = sar::platform::AudioBackendKind::Wasapi;
  physical_device.direction = sar::platform::AudioDeviceDirection::Output;
  physical_device.formats.push_back({
      48000,
      2,
      128,
      32,
      32,
      sar::platform::AudioSampleFormat::IeeeFloat,
  });
  physical_device.is_default = true;
  devices_service->add_audio_device_provider(
      std::make_unique<sar::platform::MockAudioDeviceProvider>(
          std::vector<sar::platform::AudioDeviceDescriptor>{physical_device}));

  sar::control::ControlCommand devices;
  devices.command_id = "devices-1";
  devices.type = sar::control::ControlCommandType::ListDevices;
  const auto physical_devices = send(*devices_service, devices);
  assert(physical_devices.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(physical_devices.has_devices);
  assert(physical_devices.devices.size() == 1);
  assert(physical_devices.devices[0].id == "wasapi-render-1");
  assert(physical_devices.devices[0].is_default);

  sar::control::ControlCommand create_endpoint;
  create_endpoint.command_id = "virtual-device-1";
  create_endpoint.type =
      sar::control::ControlCommandType::CreateVirtualEndpoint;
  create_endpoint.endpoint_id = "virtual-asio-1";
  create_endpoint.endpoint_label = "Virtual ASIO 1";
  const auto endpoint_created = send(*devices_service, create_endpoint);
  assert(endpoint_created.status ==
         sar::control::ControlResponseStatus::Accepted);

  devices.command_id = "devices-2";
  const auto merged_devices = send(*devices_service, devices);
  assert(merged_devices.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(merged_devices.devices.size() == 2);
  assert(merged_devices.devices[0].id == "virtual-asio-1");
  assert(merged_devices.devices[0].is_virtual);
  assert(merged_devices.devices[1].id == "wasapi-render-1");

  state.command_id = "state-with-platform-devices";
  const auto state_devices = send(*devices_service, state);
  assert(state_devices.has_session_state);
  assert(state_devices.devices.size() == 2);

  auto duplicate_device = physical_device;
  duplicate_device.id = "virtual-asio-1";
  devices_service->add_audio_device_provider(
      std::make_unique<sar::platform::MockAudioDeviceProvider>(
          std::vector<sar::platform::AudioDeviceDescriptor>{duplicate_device}));
  devices.command_id = "devices-duplicate-id";
  const auto duplicate_devices = send(*devices_service, devices);
  assert(duplicate_devices.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(!duplicate_devices.has_devices);
  assert(!duplicate_devices.errors.empty());
  assert(duplicate_devices.errors[0].code == "duplicate_device_id");

  auto invalid_devices_create =
      sar::service::EngineControlService::create(make_preset(), 41);
  assert(invalid_devices_create.ok());
  auto invalid_devices_service = invalid_devices_create.take_service();
  auto invalid_device = physical_device;
  invalid_device.formats.clear();
  invalid_devices_service->add_audio_device_provider(
      std::make_unique<sar::platform::MockAudioDeviceProvider>(
          std::vector<sar::platform::AudioDeviceDescriptor>{invalid_device}));
  devices.command_id = "devices-provider-error";
  const auto invalid_devices = send(*invalid_devices_service, devices);
  assert(invalid_devices.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(!invalid_devices.has_devices);
  assert(!invalid_devices.errors.empty());
  assert(invalid_devices.errors[0].code == "empty_device_formats");
}
