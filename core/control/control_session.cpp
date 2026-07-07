#include "core/control/control_session.h"

#include <memory>
#include <utility>

namespace sar::control {

namespace {

bool mutates_preset(ControlCommandType type) noexcept {
  switch (type) {
    case ControlCommandType::ConnectRoute:
    case ControlCommandType::DisconnectRoute:
    case ControlCommandType::SetGain:
    case ControlCommandType::SetMute:
    case ControlCommandType::LoadPreset:
      return true;

    case ControlCommandType::ListDevices:
    case ControlCommandType::CreateVirtualEndpoint:
    case ControlCommandType::RemoveVirtualEndpoint:
    case ControlCommandType::SavePreset:
    case ControlCommandType::QueryDiagnostics:
    case ControlCommandType::QueryActiveGraph:
      return false;
  }

  return false;
}

std::shared_ptr<graph::Graph> share_graph(std::unique_ptr<graph::Graph> graph) {
  return std::shared_ptr<graph::Graph>(std::move(graph));
}

}  // namespace

ControlSessionCreateResult ControlSession::create(PresetDocument initial_preset,
                                                  std::uint64_t initial_graph_version) {
  auto graph_result = build_preset_graph(initial_preset, initial_graph_version);
  if (!graph_result.ok()) {
    return ControlSessionCreateResult::failure(graph_result.errors());
  }

  auto graph = share_graph(graph_result.take_graph());
  return ControlSessionCreateResult::success(std::unique_ptr<ControlSession>(
      new ControlSession(std::move(initial_preset), graph, initial_graph_version + 1)));
}

ControlSession::ControlSession(PresetDocument preset,
                               std::shared_ptr<graph::Graph> graph,
                               std::uint64_t next_graph_version) noexcept
    : current_preset_(std::move(preset)),
      publisher_(std::move(graph)),
      next_graph_version_(next_graph_version) {}

ControlResponse ControlSession::handle(const ControlCommand& command) {
  return handle(command, {});
}

ControlResponse ControlSession::handle(const ControlCommand& command,
                                       diagnostics::EngineDiagnostics diagnostics) {
  auto validation = validate_command(command);
  if (!validation.ok()) {
    return command_rejected(command.command_id, validation.errors());
  }

  if (command.type == ControlCommandType::QueryDiagnostics) {
    return diagnostics_response(command.command_id, diagnostics);
  }

  if (command.type == ControlCommandType::QueryActiveGraph) {
    return active_graph_response(command.command_id, *publisher_.current());
  }

  if (!mutates_preset(command.type)) {
    return command_accepted(command.command_id);
  }

  auto apply_result = apply_command(current_preset_, command);
  if (!apply_result.ok()) {
    return command_rejected(command.command_id, apply_result.errors());
  }

  return publish_preset(command, apply_result.take_document());
}

void ControlSession::process(const realtime::AudioBuffer& input,
                             realtime::AudioBuffer& output,
                             diagnostics::EngineDiagnostics& diagnostics) noexcept {
  publisher_.process(input, output, diagnostics);
}

const PresetDocument& ControlSession::current_preset() const noexcept {
  return current_preset_;
}

std::shared_ptr<graph::Graph> ControlSession::current_graph() const noexcept {
  return publisher_.current();
}

std::uint64_t ControlSession::next_graph_version() const noexcept {
  return next_graph_version_;
}

ControlResponse ControlSession::publish_preset(const ControlCommand& command,
                                               PresetDocument preset) {
  auto graph_result = build_preset_graph(preset, next_graph_version_);
  if (!graph_result.ok()) {
    return command_rejected(command.command_id, graph_result.errors());
  }

  auto graph = share_graph(graph_result.take_graph());
  publisher_.publish(graph);
  current_preset_ = std::move(preset);
  ++next_graph_version_;
  return command_accepted(command.command_id);
}

ControlSessionCreateResult ControlSessionCreateResult::success(
    std::unique_ptr<ControlSession> session) {
  return {std::move(session), {}};
}

ControlSessionCreateResult ControlSessionCreateResult::failure(
    std::vector<PresetError> errors) {
  return {nullptr, std::move(errors)};
}

bool ControlSessionCreateResult::ok() const noexcept {
  return session_ != nullptr && errors_.empty();
}

ControlSession& ControlSessionCreateResult::session() noexcept {
  return *session_;
}

const ControlSession& ControlSessionCreateResult::session() const noexcept {
  return *session_;
}

std::unique_ptr<ControlSession> ControlSessionCreateResult::take_session() noexcept {
  return std::move(session_);
}

const std::vector<PresetError>& ControlSessionCreateResult::errors() const noexcept {
  return errors_;
}

ControlSessionCreateResult::ControlSessionCreateResult(
    std::unique_ptr<ControlSession> session,
    std::vector<PresetError> errors)
    : session_(std::move(session)), errors_(std::move(errors)) {}

}  // namespace sar::control
