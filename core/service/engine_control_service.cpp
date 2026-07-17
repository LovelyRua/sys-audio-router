#include "core/service/engine_control_service.h"

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
    std::unique_ptr<EngineAudioRuntime> runtime) {
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
  return EngineAudioRuntimeResult::success();
}

EngineAudioRuntimeResult EngineControlService::start_audio_runtime(
    std::uint32_t timeout_ms) {
  std::lock_guard lock(control_mutex_);
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
    return EngineAudioRuntimeResult::failure({
        {"audio_runtime_graph_stale",
         "Rebuild the audio runtime for the current graph before starting it."},
    });
  }
  return audio_runtime_->start(timeout_ms);
}

void EngineControlService::stop_audio_runtime() noexcept {
  std::lock_guard lock(control_mutex_);
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
  if (audio_runtime_ && audio_runtime_->running() &&
      control::control_command_mutates_preset(decoded.command.type)) {
    return control::encode_control_response(control::command_rejected(
        decoded.command.command_id,
        {{"audio_runtime_graph_change_requires_restart",
          "Stop the audio runtime before changing the active graph."}}));
  }
  diagnostics::EngineDiagnostics diagnostics;
  if (decoded.command.type == control::ControlCommandType::QueryDiagnostics &&
      audio_runtime_) {
    diagnostics = audio_runtime_->diagnostics();
  }
  return control::encode_control_response(
      session_->handle(decoded.command, diagnostics));
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
