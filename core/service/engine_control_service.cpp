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
  return control::encode_control_response(session_->handle(decoded.command));
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
