#include "core/service/engine_control_service.h"

#include "core/service/audio_runtime_matrix_preset.h"

#include <algorithm>
#include <exception>
#include <string>
#include <utility>

namespace sar::service {

namespace {

control::WasapiRecoveryState convert_recovery_state(
    EngineAudioRecoveryState state) noexcept {
  switch (state) {
    case EngineAudioRecoveryState::Stopped:
      return control::WasapiRecoveryState::Stopped;
    case EngineAudioRecoveryState::Opening:
      return control::WasapiRecoveryState::Opening;
    case EngineAudioRecoveryState::Running:
      return control::WasapiRecoveryState::Running;
    case EngineAudioRecoveryState::Quiescing:
      return control::WasapiRecoveryState::Quiescing;
    case EngineAudioRecoveryState::Backoff:
      return control::WasapiRecoveryState::Backoff;
    case EngineAudioRecoveryState::Faulted:
      return control::WasapiRecoveryState::Faulted;
  }
  return control::WasapiRecoveryState::Faulted;
}

control::WasapiRuntimeHealth convert_runtime_health(
    EngineAudioRuntimeHealth health) noexcept {
  switch (health) {
    case EngineAudioRuntimeHealth::Stopped:
      return control::WasapiRuntimeHealth::Stopped;
    case EngineAudioRuntimeHealth::Healthy:
      return control::WasapiRuntimeHealth::Healthy;
    case EngineAudioRuntimeHealth::Degraded:
      return control::WasapiRuntimeHealth::Degraded;
    case EngineAudioRuntimeHealth::Faulted:
      return control::WasapiRuntimeHealth::Faulted;
  }
  return control::WasapiRuntimeHealth::Faulted;
}

control::WasapiRecoveryDiagnostics convert_recovery_diagnostics(
    const EngineAudioRecoveryDiagnostics& recovery) {
  return {
      .state = convert_recovery_state(recovery.state),
      .runtime_health = convert_runtime_health(recovery.runtime_health),
      .runtime_reason_code = recovery.runtime_reason_code,
      .recovery_episode_count = recovery.recovery_episode_count,
      .successful_recovery_count = recovery.successful_recovery_count,
      .failed_recovery_count = recovery.failed_recovery_count,
      .last_recovery_duration_ms = recovery.last_recovery_duration_ms,
      .maximum_recovery_duration_ms = recovery.maximum_recovery_duration_ms,
      .endpoint_notification_reopen_count =
          recovery.endpoint_notification_reopen_count,
      .endpoint_notification_reset_failure_count =
          recovery.endpoint_notification_reset_failure_count,
      .endpoint_notification_reopen_pending =
          recovery.endpoint_notification_reopen_pending,
      .wait_timeout_cycles = recovery.wait_timeout_cycles,
      .capture_discontinuity_cycles = recovery.capture_discontinuity_cycles,
      .render_fifo_underflow_frames = recovery.render_fifo_underflow_frames,
      .maximum_render_recovery_silence_frames =
          recovery.maximum_render_recovery_silence_frames,
      .maximum_consecutive_capture_rate_clamped_frames =
          recovery.maximum_consecutive_capture_rate_clamped_frames,
  };
}

bool updates_realtime_route_parameters(
    control::ControlCommandType type) noexcept {
  return type == control::ControlCommandType::ConnectRoute ||
         type == control::ControlCommandType::DisconnectRoute ||
         type == control::ControlCommandType::SetGain ||
         type == control::ControlCommandType::SetMute;
}

}  // namespace

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

void EngineControlService::set_wasapi_recovery_diagnostics_provider(
    WasapiRecoveryDiagnosticsProvider provider) {
  std::lock_guard lock(control_mutex_);
  wasapi_recovery_diagnostics_provider_ = std::move(provider);
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

  std::optional<control::PreparedPresetUpdate> matrix_update;
  const bool changes_matrix_topology =
      configuration.mode == control::AudioRuntimeMode::WasapiMatrix ||
      (audio_runtime_configuration_ &&
       audio_runtime_configuration_->mode ==
           control::AudioRuntimeMode::WasapiMatrix);
  if (changes_matrix_topology) {
    const auto previous = audio_runtime_configuration_.value_or(
        control::AudioRuntimeConfiguration{});
    auto reconciled = reconcile_audio_runtime_matrix_preset(
        session_->current_preset(), previous, configuration);
    if (!reconciled.ok()) {
      std::vector<EngineAudioRuntimeError> errors;
      errors.reserve(reconciled.errors().size());
      for (const auto& error : reconciled.errors()) {
        errors.push_back({error.code, error.message});
      }
      return EngineAudioRuntimeResult::failure(std::move(errors));
    }
    control::ControlCommand load;
    load.command_id = "runtime-matrix-reconcile";
    load.type = control::ControlCommandType::LoadPreset;
    load.preset = reconciled.take_preset();
    auto prepared = session_->prepare_preset_update(load);
    if (!prepared.ok()) {
      std::vector<EngineAudioRuntimeError> errors;
      errors.reserve(prepared.errors().size());
      for (const auto& error : prepared.errors()) {
        errors.push_back({error.code, error.message});
      }
      return EngineAudioRuntimeResult::failure(std::move(errors));
    }
    matrix_update.emplace(prepared.take_update());
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
    const auto graph = matrix_update ? matrix_update->graph
                                     : session_->current_graph();
    const auto& matrix = matrix_update ? matrix_update->preset.matrix
                                       : session_->current_preset().matrix;
    auto rebuilt = audio_runtime_configurator_(configuration, graph, matrix);
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

    if (matrix_update && preset_commit_observer_) {
      std::vector<control::PresetError> observer_errors;
      try {
        observer_errors = preset_commit_observer_(
            matrix_update->preset, matrix_update->graph_version);
      } catch (const std::exception& error) {
        observer_errors.push_back({"preset_commit_observer_exception",
                                   error.what()});
      } catch (...) {
        observer_errors.push_back({
            "preset_commit_observer_exception",
            "Preset commit observer raised an unknown exception.",
        });
      }
      if (!observer_errors.empty()) {
        runtime->stop();
        std::vector<EngineAudioRuntimeError> errors;
        errors.reserve(observer_errors.size());
        for (const auto& error : observer_errors) {
          errors.push_back({error.code, error.message});
        }
        return restore_previous_runtime(std::move(errors));
      }
    }

    audio_runtime_ = std::move(runtime);
    audio_runtime_configuration_ = std::move(configuration);
    if (matrix_update) {
      session_->commit_preset_update(std::move(*matrix_update));
    }
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
  auto rebuilt = build_audio_runtime_locked(
      session_->current_graph(), session_->current_preset().matrix);
  if (!rebuilt.ok()) {
    return EngineAudioRuntimeResult::failure(rebuilt.errors());
  }
  audio_runtime_ = rebuilt.take_runtime();
  return EngineAudioRuntimeResult::success();
}

EngineAudioRuntimeBuildResult
EngineControlService::build_audio_runtime_locked(
    std::shared_ptr<graph::Graph> graph,
    const control::PresetRouteMatrix& matrix) {
  if (!audio_runtime_builder_ &&
      !(audio_runtime_configurator_ && audio_runtime_configuration_)) {
    return EngineAudioRuntimeBuildResult::failure({
        {"audio_runtime_graph_stale",
         "Rebuild the audio runtime for the current graph before starting it."},
    });
  }

  try {
    auto rebuilt = audio_runtime_configurator_ && audio_runtime_configuration_
                       ? audio_runtime_configurator_(
                             *audio_runtime_configuration_, std::move(graph),
                             matrix)
                       : audio_runtime_builder_(std::move(graph));
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
      if (std::ranges::equal(replayed->request, request)) {
        return replayed->response;
      }
      return control::encode_control_response(control::command_rejected(
          decoded.command.command_id,
          {{"command_id_conflict",
            "Control command ID was already used for a different request."}}));
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
  const bool realtime_route_update =
      updates_realtime_route_parameters(decoded.command.type);
  const bool apply_route_update_live =
      realtime_route_update && audio_runtime_ && audio_runtime_->running();
  const bool restart_runtime_after_preset_commit =
      mutates_preset && !realtime_route_update && audio_runtime_ &&
      audio_runtime_->running();
  if (restart_runtime_after_preset_commit && !audio_runtime_builder_ &&
      !(audio_runtime_configurator_ && audio_runtime_configuration_)) {
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

      auto rebuilt = build_audio_runtime_locked(update.graph,
                                                update.preset.matrix);
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
    if (apply_route_update_live &&
        !audio_runtime_->apply_realtime_graph_parameters(*update.graph)) {
      if (preset_commit_observer_) {
        try {
          const auto current_graph = session_->current_graph();
          static_cast<void>(preset_commit_observer_(
              session_->current_preset(),
              current_graph ? current_graph->version() : 0));
        } catch (...) {
          // The original graph remains authoritative when rollback reporting
          // itself fails.
        }
      }
      return control::encode_control_response(control::command_rejected(
          decoded.command.command_id,
          {{"audio_runtime_realtime_parameter_update_unsupported",
            "The running audio runtime cannot apply route parameters "
            "without restarting."}}));
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
  if (decoded.command.type == control::ControlCommandType::QueryDiagnostics &&
      response.has_diagnostics) {
    try {
      const auto runtime_recovery =
          audio_runtime_ ? audio_runtime_->recovery_diagnostics() : std::nullopt;
      const auto recovery = runtime_recovery
          ? std::optional{
                convert_recovery_diagnostics(*runtime_recovery)}
          : wasapi_recovery_diagnostics_provider_
                ? wasapi_recovery_diagnostics_provider_()
                : std::nullopt;
      if (recovery) {
        response.wasapi_recovery = *recovery;
        response.has_wasapi_recovery = true;
      }
      if (audio_runtime_) {
        const auto endpoints = audio_runtime_->endpoint_diagnostics();
        response.endpoint_diagnostics.reserve(endpoints.size());
        for (const auto& endpoint : endpoints) {
          control::AudioEndpointRuntimeDiagnostics converted{
              .endpoint_id = endpoint.endpoint_id,
              .role = endpoint.role == EngineAudioEndpointRole::Master
                          ? control::AudioEndpointRuntimeRole::Master
                          : control::AudioEndpointRuntimeRole::Follower,
              .diagnostics = endpoint.diagnostics,
              .queue_fill_frames = endpoint.queue_fill_frames,
              .correction_ppm = endpoint.correction_ppm,
          };
          if (endpoint.recovery) {
            converted.recovery =
                convert_recovery_diagnostics(*endpoint.recovery);
          }
          response.endpoint_diagnostics.push_back(std::move(converted));
        }
      }
    } catch (...) {
      // Recovery telemetry must not make the control plane unavailable.
    }
  }
  if (decoded.command.type == control::ControlCommandType::ListDevices ||
      decoded.command.type == control::ControlCommandType::QuerySessionState) {
    response = append_platform_devices_locked(std::move(response));
  }
  return control::encode_control_response(response);
  };

  auto response = execute();
  if (!decoded.command.command_id.empty()) {
    remember_response_locked(decoded.command.command_id, request, response);
  }
  return response;
}

const EngineControlService::ReplayedResponse*
EngineControlService::find_replayed_response_locked(
    std::string_view command_id) const noexcept {
  for (const auto& replayed : replayed_responses_) {
    if (replayed.command_id == command_id) {
      return &replayed;
    }
  }
  return nullptr;
}

void EngineControlService::remember_response_locked(
    std::string command_id,
    std::span<const std::uint8_t> request,
    const control::ControlWireEncodeResult& response) {
  const auto entry_bytes = request.size() + response.bytes.size();
  if (entry_bytes > kMaxReplayedResponseBytes) {
    return;
  }

  while (!replayed_responses_.empty() &&
         (replayed_responses_.size() >= kMaxReplayedResponses ||
          replayed_response_bytes_ + entry_bytes >
              kMaxReplayedResponseBytes)) {
    const auto& oldest = replayed_responses_.front();
    replayed_response_bytes_ -=
        oldest.request.size() + oldest.response.bytes.size();
    replayed_responses_.pop_front();
  }

  replayed_response_bytes_ += entry_bytes;
  replayed_responses_.push_back(
      {std::move(command_id),
       std::vector<std::uint8_t>(request.begin(), request.end()), response});
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
