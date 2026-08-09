#include "core/service/engine_control_service.h"

#include <exception>
#include <string>
#include <utility>

namespace sar::service {

EngineControlServiceCreateResult EngineControlService::create(
    control::PresetDocument initial_preset,
    std::uint64_t initial_graph_version) {
  auto session_result = control::ControlSession::create(
      std::move(initial_preset), initial_graph_version);
  if (!session_result.ok()) {
    return EngineControlServiceCreateResult::failure(session_result.errors());
  }
  return EngineControlServiceCreateResult::success(
      std::unique_ptr<EngineControlService>(
          new EngineControlService(session_result.take_session())));
}

EngineControlService::~EngineControlService() {
  stop_audio_runtime();
}

EngineAudioRuntimeResult EngineControlService::install_audio_runtime(
    std::unique_ptr<EngineAudioRuntime> runtime,
    EngineAudioRuntimeBuilder builder) {
  if (!runtime) {
    return EngineAudioRuntimeResult::failure({
        {"null_audio_runtime", "Engine audio runtime must not be null."},
    });
  }

  std::lock_guard lock(control_mutex_);
  if (audio_runtime_ && audio_runtime_->running()) {
    return EngineAudioRuntimeResult::failure({
        {"audio_runtime_running",
         "Stop the active audio runtime before replacing it."},
    });
  }
  audio_runtime_ = std::move(runtime);
  audio_runtime_builder_ = std::move(builder);
  audio_runtime_configuration_.reset();
  return EngineAudioRuntimeResult::success();
}

void EngineControlService::set_audio_runtime_configurator(
    EngineAudioRuntimeConfigurator configurator) {
  std::lock_guard lock(control_mutex_);
  audio_runtime_configurator_ = std::move(configurator);
}

void EngineControlService::set_preset_commit_observer(
    EnginePresetCommitObserver observer) {
  std::lock_guard lock(control_mutex_);
  preset_commit_observer_ = std::move(observer);
}

EngineAudioRuntimeResult EngineControlService::configure_audio_runtime(
    control::AudioRuntimeConfiguration configuration) {
  std::lock_guard lock(control_mutex_);
  return configure_audio_runtime_locked(std::move(configuration));
}

void EngineControlService::add_audio_device_provider(
    std::unique_ptr<platform::AudioDeviceProvider> provider) {
  std::lock_guard lock(control_mutex_);
  audio_device_registry_.add_provider(std::move(provider));
}

EngineAudioRuntimeResult EngineControlService::configure_audio_runtime_locked(
    control::AudioRuntimeConfiguration configuration) {
  if (!audio_runtime_configurator_) {
    return EngineAudioRuntimeResult::failure({
        {"audio_runtime_configurator_not_installed",
         "Install an audio runtime configurator before configuring the engine."},
    });
  }

  const bool restart_after_configure =
      audio_runtime_ && audio_runtime_->running();
  std::unique_ptr<EngineAudioRuntime> previous_runtime;
  if (restart_after_configure) {
    audio_runtime_->stop();
    previous_runtime = std::move(audio_runtime_);
  }

  const auto restore_previous_runtime =
      [&](std::vector<EngineAudioRuntimeError> errors) {
        if (!restart_after_configure) {
          return EngineAudioRuntimeResult::failure(std::move(errors));
        }
        audio_runtime_ = std::move(previous_runtime);
        try {
          const auto restored = audio_runtime_->start(10);
          for (const auto& error : restored.errors()) {
            errors.push_back({
                "audio_runtime_restore_failed_" + error.code,
                "The audio configuration was rejected and the previous "
                "runtime could not be restored: " + error.message,
                error.native_hresult,
                error.native_win32_code,
            });
          }
        } catch (const std::exception& error) {
          errors.push_back({
              "audio_runtime_restore_exception",
              std::string("The previous audio runtime threw while being "
                          "restored: ") + error.what(),
          });
        } catch (...) {
          errors.push_back({
              "audio_runtime_restore_exception",
              "The previous audio runtime threw an unknown exception while "
              "being restored.",
          });
        }
        return EngineAudioRuntimeResult::failure(std::move(errors));
      };

  try {
    auto rebuilt =
        audio_runtime_configurator_(configuration, session_->current_graph());
    if (!rebuilt.ok()) {
      if (rebuilt.errors().empty()) {
        return restore_previous_runtime({
            {"audio_runtime_configurator_returned_null",
             "Audio runtime configurator returned no runtime."},
        });
      }
      return restore_previous_runtime(rebuilt.errors());
    }
    auto runtime = rebuilt.take_runtime();
    if (!runtime) {
      return restore_previous_runtime({
          {"audio_runtime_configurator_returned_null",
           "Audio runtime configurator returned no runtime."},
      });
    }

    if (restart_after_configure) {
      const auto started = runtime->start(10);
      if (!started.ok()) {
        runtime->stop();
        return restore_previous_runtime(started.errors());
      }
    }

    const auto configurator = audio_runtime_configurator_;
    audio_runtime_builder_ =
        [configurator, configuration](std::shared_ptr<graph::Graph> graph) {
          return configurator(configuration, std::move(graph));
        };
    audio_runtime_ = std::move(runtime);
    audio_runtime_configuration_ = std::move(configuration);
  } catch (const std::exception& error) {
    return restore_previous_runtime({
        {"audio_runtime_configurator_exception", error.what()},
    });
  } catch (...) {
    return restore_previous_runtime({
        {"audio_runtime_configurator_exception",
         "Audio runtime configurator raised an unknown exception."},
    });
  }
  return EngineAudioRuntimeResult::success();
}

EngineAudioRuntimeResult EngineControlService::start_audio_runtime(
    std::uint32_t timeout_ms) {
  std::lock_guard lock(control_mutex_);
  return start_audio_runtime_locked(timeout_ms);
}

EngineAudioRuntimeResult EngineControlService::start_audio_runtime_locked(
    std::uint32_t timeout_ms) {
  if (!audio_runtime_) {
    return EngineAudioRuntimeResult::failure({
        {"audio_runtime_not_installed",
         "Install an audio runtime before starting the engine."},
    });
  }
  if (audio_runtime_->running()) {
    return EngineAudioRuntimeResult::failure({
        {"audio_runtime_already_running", "Engine audio runtime is already running."},
    });
  }
  if (audio_runtime_->graph_version() != session_->current_graph()->version()) {
    const auto rebuild = rebuild_audio_runtime_locked();
    if (!rebuild.ok()) {
      return rebuild;
    }
  }
  return audio_runtime_->start(timeout_ms);
}

EngineAudioRuntimeResult EngineControlService::rebuild_audio_runtime_locked() {
  auto rebuilt = build_audio_runtime_locked(session_->current_graph());
  if (!rebuilt.ok()) {
    return EngineAudioRuntimeResult::failure(rebuilt.errors());
  }
  audio_runtime_ = rebuilt.take_runtime();
  return EngineAudioRuntimeResult::success();
}

EngineAudioRuntimeBuildResult
EngineControlService::build_audio_runtime_locked(
    std::shared_ptr<graph::Graph> graph) {
  if (!audio_runtime_builder_) {
    return EngineAudioRuntimeBuildResult::failure({
        {"audio_runtime_graph_stale",
         "Rebuild the audio runtime for the current graph before starting it."},
    });
  }

  try {
    auto rebuilt = audio_runtime_builder_(std::move(graph));
    if (!rebuilt.ok()) {
      if (rebuilt.errors().empty()) {
        return EngineAudioRuntimeBuildResult::failure({
            {"audio_runtime_builder_returned_null",
             "Audio runtime builder returned no runtime."},
        });
      }
      return EngineAudioRuntimeBuildResult::failure(rebuilt.errors());
    }
    auto runtime = rebuilt.take_runtime();
    if (!runtime) {
      return EngineAudioRuntimeBuildResult::failure({
          {"audio_runtime_builder_returned_null",
           "Audio runtime builder returned no runtime."},
      });
    }
    return EngineAudioRuntimeBuildResult::success(std::move(runtime));
  } catch (const std::exception& error) {
    return EngineAudioRuntimeBuildResult::failure({
        {"audio_runtime_builder_exception", error.what()},
    });
  } catch (...) {
    return EngineAudioRuntimeBuildResult::failure({
        {"audio_runtime_builder_exception",
         "Audio runtime builder raised an unknown exception."},
    });
  }
}

void EngineControlService::stop_audio_runtime() noexcept {
  std::lock_guard lock(control_mutex_);
  stop_audio_runtime_locked();
}

void EngineControlService::stop_audio_runtime_locked() noexcept {
  if (audio_runtime_) {
    audio_runtime_->stop();
  }
}

bool EngineControlService::has_audio_runtime() const noexcept {
  std::lock_guard lock(control_mutex_);
  return audio_runtime_ != nullptr;
}

bool EngineControlService::audio_runtime_running() const noexcept {
  std::lock_guard lock(control_mutex_);
  return audio_runtime_ && audio_runtime_->running();
}

diagnostics::EngineDiagnostics EngineControlService::audio_runtime_diagnostics()
    const {
  std::lock_guard lock(control_mutex_);
  return audio_runtime_ ? audio_runtime_->diagnostics()
                        : diagnostics::EngineDiagnostics{};
}

control::SessionDocument EngineControlService::session_document() const {
  std::lock_guard lock(control_mutex_);
  control::SessionDocument document;
  document.preset = session_->current_preset();
  if (audio_runtime_configuration_) {
    document.audio_runtime = *audio_runtime_configuration_;
    document.auto_start = audio_runtime_ && audio_runtime_->running();
  }
  return document;
}

std::unique_ptr<graph::Graph> EngineControlService::build_client_graph(
    std::uint32_t sample_rate,
    std::uint32_t frames_per_block,
    std::uint32_t input_channels,
    std::uint32_t output_channels) const {
  std::lock_guard lock(control_mutex_);
  const auto& preset = session_->current_preset();
  if (sample_rate != preset.sample_rate ||
      input_channels != output_channels ||
      input_channels != preset.matrix.inputs.size() ||
      output_channels != preset.matrix.outputs.size()) {
    return nullptr;
  }
  const auto current = session_->current_graph();
  const auto version = current ? current->version() : 1;
  auto client_preset = preset;
  client_preset.frames_per_block = frames_per_block;
  auto built = control::build_preset_graph(client_preset, version);
  return built.ok() ? built.take_graph() : nullptr;
}

control::ControlWireEncodeResult EngineControlService::handle_wire_request(
    std::span<const std::uint8_t> request) {
  const auto decoded = control::decode_control_command(request);
  if (!decoded.ok()) {
    auto response = control::command_rejected(
        {}, {{"invalid_control_wire_request",
              "Control wire request could not be decoded at byte " +
                  std::to_string(decoded.error.offset) + "."}});
    return control::encode_control_response(response);
  }

  std::lock_guard lock(control_mutex_);
  if (!decoded.command.command_id.empty()) {
    if (const auto* replayed =
            find_replayed_response_locked(decoded.command.command_id)) {
      return *replayed;
    }
  }

  const auto execute = [&]() -> control::ControlWireEncodeResult {
  const auto validation = control::validate_command(decoded.command);
  if (!validation.ok()) {
    return control::encode_control_response(control::command_rejected(
        decoded.command.command_id, validation.errors()));
  }
  if (decoded.command.type == control::ControlCommandType::QueryAudioRuntime) {
    return control::encode_control_response(
        audio_runtime_state_response_locked(decoded.command.command_id));
  }
  if (decoded.command.type == control::ControlCommandType::StartAudioRuntime) {
    const auto result = start_audio_runtime_locked(10);
    if (!result.ok()) {
      std::vector<control::PresetError> errors;
      errors.reserve(result.errors().size());
      for (const auto& error : result.errors()) {
        errors.push_back({error.code, error.message});
      }
      return control::encode_control_response(control::command_rejected(
          decoded.command.command_id, std::move(errors)));
    }
    return control::encode_control_response(
        audio_runtime_state_response_locked(decoded.command.command_id));
  }
  if (decoded.command.type == control::ControlCommandType::StopAudioRuntime) {
    stop_audio_runtime_locked();
    return control::encode_control_response(
        audio_runtime_state_response_locked(decoded.command.command_id));
  }
  if (decoded.command.type ==
      control::ControlCommandType::ConfigureAudioRuntime) {
    const auto result =
        configure_audio_runtime_locked(decoded.command.audio_runtime);
    if (!result.ok()) {
      std::vector<control::PresetError> errors;
      errors.reserve(result.errors().size());
      for (const auto& error : result.errors()) {
        errors.push_back({error.code, error.message});
      }
      return control::encode_control_response(control::command_rejected(
          decoded.command.command_id, std::move(errors)));
    }
    return control::encode_control_response(
        audio_runtime_state_response_locked(decoded.command.command_id));
  }
  const auto mutates_preset =
      control::control_command_mutates_preset(decoded.command.type);
  const bool restart_runtime_after_preset_commit =
      mutates_preset && audio_runtime_ && audio_runtime_->running();
  if (restart_runtime_after_preset_commit && !audio_runtime_builder_) {
    return control::encode_control_response(control::command_rejected(
        decoded.command.command_id,
        {{"audio_runtime_graph_change_requires_restart",
          "The running audio runtime cannot rebuild its graph automatically."}}));
  }
  diagnostics::EngineDiagnostics diagnostics;
  if (decoded.command.type == control::ControlCommandType::QueryDiagnostics &&
      audio_runtime_) {
    diagnostics = audio_runtime_->diagnostics();
  }
  if (mutates_preset && preset_commit_in_progress_) {
    return control::encode_control_response(control::command_rejected(
        decoded.command.command_id,
        {{"reentrant_preset_mutation_rejected",
          "A preset observer cannot issue another preset mutation while a "
          "commit is pending."}}));
  }

  if (mutates_preset) {
    auto prepared = session_->prepare_preset_update(decoded.command);
    if (!prepared.ok()) {
      return control::encode_control_response(control::command_rejected(
          decoded.command.command_id, prepared.errors()));
    }

    auto update = prepared.take_update();
    std::unique_ptr<EngineAudioRuntime> previous_runtime;
    if (restart_runtime_after_preset_commit) {
      stop_audio_runtime_locked();
      previous_runtime = std::move(audio_runtime_);

      auto rebuilt = build_audio_runtime_locked(update.graph);
      if (!rebuilt.ok()) {
        std::vector<control::PresetError> errors;
        errors.reserve(rebuilt.errors().size());
        for (const auto& error : rebuilt.errors()) {
          errors.push_back({
              "audio_runtime_rebuild_failed_" + error.code,
              "The preset change was not committed because the replacement "
              "audio runtime could not be built: " + error.message,
          });
        }
        audio_runtime_ = std::move(previous_runtime);
        const auto restored = start_audio_runtime_locked(10);
        for (const auto& error : restored.errors()) {
          errors.push_back({
              "audio_runtime_restore_failed_" + error.code,
              "The preset change was rejected and the previous audio runtime "
              "could not be restored: " + error.message,
          });
        }
        return control::encode_control_response(control::command_rejected(
            decoded.command.command_id, std::move(errors)));
      }

      audio_runtime_ = rebuilt.take_runtime();
      const auto started = audio_runtime_->start(10);
      if (!started.ok()) {
        std::vector<control::PresetError> errors;
        errors.reserve(started.errors().size());
        for (const auto& error : started.errors()) {
          errors.push_back({
              "audio_runtime_replacement_start_failed_" + error.code,
              "The preset change was not committed because the replacement "
              "audio runtime could not start: " + error.message,
          });
        }
        audio_runtime_->stop();
        audio_runtime_ = std::move(previous_runtime);
        const auto restored = start_audio_runtime_locked(10);
        for (const auto& error : restored.errors()) {
          errors.push_back({
              "audio_runtime_restore_failed_" + error.code,
              "The preset change was rejected and the previous audio runtime "
              "could not be restored: " + error.message,
          });
        }
        return control::encode_control_response(control::command_rejected(
            decoded.command.command_id, std::move(errors)));
      }
    }

    const auto reject_and_restore_runtime =
        [&](std::vector<control::PresetError> errors) {
          if (restart_runtime_after_preset_commit) {
            audio_runtime_->stop();
            audio_runtime_ = std::move(previous_runtime);
            const auto restored = start_audio_runtime_locked(10);
            for (const auto& error : restored.errors()) {
              errors.push_back({
                  "audio_runtime_restore_failed_" + error.code,
                  "The preset change was rejected and the previous audio "
                  "runtime could not be restored: " + error.message,
              });
            }
          }
          return control::encode_control_response(control::command_rejected(
              decoded.command.command_id, std::move(errors)));
        };

    std::vector<control::PresetError> observer_errors;
    if (preset_commit_observer_) {
      preset_commit_in_progress_ = true;
      try {
        observer_errors = preset_commit_observer_(
            update.preset, update.graph_version);
      } catch (const std::exception& error) {
        observer_errors.push_back({
            "preset_commit_observer_exception",
            std::string("The preset commit observer threw an exception: ") +
                error.what(),
        });
      } catch (...) {
        observer_errors.push_back({
            "preset_commit_observer_exception",
            "The preset commit observer threw an unknown exception.",
        });
      }
      preset_commit_in_progress_ = false;
    }
    if (!observer_errors.empty()) {
      return reject_and_restore_runtime(std::move(observer_errors));
    }
    session_->commit_preset_update(std::move(update));

    auto response = control::preset_response(
        decoded.command.command_id, session_->current_preset());
    if (audio_runtime_) {
      const auto runtime =
          audio_runtime_state_response_locked(decoded.command.command_id);
      response.audio_runtime = runtime.audio_runtime;
      response.has_audio_runtime_state = true;
    }
    return control::encode_control_response(response);
  }
  auto response = session_->handle(decoded.command, diagnostics);
  if (decoded.command.type == control::ControlCommandType::ListDevices ||
      decoded.command.type == control::ControlCommandType::QuerySessionState) {
    response = append_platform_devices_locked(std::move(response));
  }
  return control::encode_control_response(response);
  };

  auto response = execute();
  if (!decoded.command.command_id.empty()) {
    remember_response_locked(decoded.command.command_id, response);
  }
  return response;
}

const control::ControlWireEncodeResult*
EngineControlService::find_replayed_response_locked(
    std::string_view command_id) const noexcept {
  for (const auto& replayed : replayed_responses_) {
    if (replayed.command_id == command_id) {
      return &replayed.response;
    }
  }
  return nullptr;
}

void EngineControlService::remember_response_locked(
    std::string command_id,
    const control::ControlWireEncodeResult& response) {
  if (response.bytes.size() > kMaxReplayedResponseBytes) {
    return;
  }

  while (!replayed_responses_.empty() &&
         (replayed_responses_.size() >= kMaxReplayedResponses ||
          replayed_response_bytes_ + response.bytes.size() >
              kMaxReplayedResponseBytes)) {
    replayed_response_bytes_ -= replayed_responses_.front().response.bytes.size();
    replayed_responses_.pop_front();
  }

  replayed_response_bytes_ += response.bytes.size();
  replayed_responses_.push_back({std::move(command_id), response});
}

control::ControlResponse EngineControlService::audio_runtime_state_response_locked(
    std::string command_id) const {
  auto response = control::audio_runtime_state_response(
      std::move(command_id),
      audio_runtime_ != nullptr,
      audio_runtime_ && audio_runtime_->running(),
      audio_runtime_ ? audio_runtime_->graph_version() : 0);
  if (audio_runtime_configuration_) {
    response.audio_runtime.configured = true;
    response.audio_runtime.configuration = *audio_runtime_configuration_;
  }
  return response;
}

control::ControlResponse EngineControlService::append_platform_devices_locked(
    control::ControlResponse response) const {
  if (response.status == control::ControlResponseStatus::Rejected) {
    return response;
  }

  const auto platform_devices = audio_device_registry_.list_devices();
  if (!platform_devices.ok()) {
    std::vector<control::PresetError> errors;
    errors.reserve(platform_devices.errors().size());
    for (const auto& error : platform_devices.errors()) {
      errors.push_back({error.code, error.message});
    }
    return control::command_rejected(response.command_id, std::move(errors));
  }

  response.devices.reserve(response.devices.size() +
                           platform_devices.devices().size());
  for (const auto& device : platform_devices.devices()) {
    response.devices.push_back(device);
  }
  const auto validation = platform::validate_audio_devices(response.devices);
  if (!validation.empty()) {
    std::vector<control::PresetError> errors;
    errors.reserve(validation.size());
    for (const auto& error : validation) {
      errors.push_back({error.code, error.message});
    }
    return control::command_rejected(response.command_id, std::move(errors));
  }
  response.has_devices = true;
  return response;
}

void EngineControlService::process(
    const realtime::AudioBuffer& input,
    realtime::AudioBuffer& output,
    diagnostics::EngineDiagnostics& diagnostics) noexcept {
  session_->process(input, output, diagnostics);
}

const control::ControlSession& EngineControlService::session() const noexcept {
  return *session_;
}

EngineControlService::EngineControlService(
    std::unique_ptr<control::ControlSession> session) noexcept
    : session_(std::move(session)) {}

EngineControlServiceCreateResult EngineControlServiceCreateResult::success(
    std::unique_ptr<EngineControlService> service) {
  return {std::move(service), {}};
}

EngineControlServiceCreateResult EngineControlServiceCreateResult::failure(
    std::vector<control::PresetError> errors) {
  return {nullptr, std::move(errors)};
}

bool EngineControlServiceCreateResult::ok() const noexcept {
  return service_ != nullptr && errors_.empty();
}

EngineControlService& EngineControlServiceCreateResult::service() noexcept {
  return *service_;
}

std::unique_ptr<EngineControlService>
EngineControlServiceCreateResult::take_service() noexcept {
  return std::move(service_);
}

const std::vector<control::PresetError>&
EngineControlServiceCreateResult::errors() const noexcept {
  return errors_;
}

EngineControlServiceCreateResult::EngineControlServiceCreateResult(
    std::unique_ptr<EngineControlService> service,
    std::vector<control::PresetError> errors) noexcept
    : service_(std::move(service)), errors_(std::move(errors)) {}

}  // namespace sar::service
