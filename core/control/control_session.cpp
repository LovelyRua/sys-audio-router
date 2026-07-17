#include "core/control/control_session.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace sar::control {

namespace {

std::shared_ptr<graph::Graph> share_graph(std::unique_ptr<graph::Graph> graph) {
  return std::shared_ptr<graph::Graph>(std::move(graph));
}

std::vector<PresetError> convert_errors(
    const std::vector<platform::VirtualEndpointError>& errors) {
  std::vector<PresetError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

std::vector<PresetError> convert_errors(
    const std::vector<platform::AudioDeviceError>& errors) {
  std::vector<PresetError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

platform::VirtualEndpointDescriptor make_virtual_endpoint(
    const ControlCommand& command,
    const PresetDocument& preset) {
  const auto endpoint_channels = std::max(preset.matrix.inputs.size(),
                                          preset.matrix.outputs.size());

  platform::VirtualEndpointDescriptor endpoint;
  endpoint.id = command.endpoint_id;
  endpoint.label = command.endpoint_label;
  endpoint.backend = platform::AudioBackendKind::VirtualAsio;
  endpoint.direction = platform::AudioDeviceDirection::Duplex;
  endpoint.format.sample_rate = preset.sample_rate;
  endpoint.format.channels = static_cast<std::uint32_t>(endpoint_channels);
  endpoint.format.frames_per_block = preset.frames_per_block;
  return endpoint;
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

  if (command.type == ControlCommandType::SavePreset) {
    return preset_response(command.command_id, current_preset_);
  }

  if (command.type == ControlCommandType::QuerySessionState) {
    auto result = virtual_endpoints_.list_devices();
    if (!result.ok()) {
      return command_rejected(command.command_id, convert_errors(result.errors()));
    }
    return session_state_response(command.command_id,
                                  current_preset_,
                                  result.devices(),
                                  *publisher_.current(),
                                  next_graph_version_);
  }

  if (command.type == ControlCommandType::QueryAudioRuntime ||
      command.type == ControlCommandType::StartAudioRuntime ||
      command.type == ControlCommandType::StopAudioRuntime ||
      command.type == ControlCommandType::ConfigureAudioRuntime) {
    return command_rejected(command.command_id, {
        {"audio_runtime_command_requires_service",
         "Audio runtime commands must be handled by the engine service."},
    });
  }

  if (command.type == ControlCommandType::ListDevices) {
    auto result = virtual_endpoints_.list_devices();
    if (!result.ok()) {
      return command_rejected(command.command_id, convert_errors(result.errors()));
    }
    return device_list_response(command.command_id, result.devices());
  }

  if (command.type == ControlCommandType::CreateVirtualEndpoint) {
    auto result = virtual_endpoints_.add_endpoint(
        make_virtual_endpoint(command, current_preset_));
    if (!result.ok()) {
      return command_rejected(command.command_id, convert_errors(result.errors()));
    }
    return command_accepted(command.command_id);
  }

  if (command.type == ControlCommandType::RemoveVirtualEndpoint) {
    auto result = virtual_endpoints_.remove_endpoint(command.endpoint_id);
    if (!result.ok()) {
      return command_rejected(command.command_id, convert_errors(result.errors()));
    }
    return command_accepted(command.command_id);
  }

  if (!control_command_mutates_preset(command.type)) {
    return command_accepted(command.command_id);
  }

  auto apply_result = apply_command(current_preset_, command);
  if (!apply_result.ok()) {
    return command_rejected(command.command_id, apply_result.errors());
  }

  return publish_preset(command, apply_result.take_document());
}

ControlResponse ControlSession::handle_batch(
    std::string command_id,
    const std::vector<ControlCommand>& commands) {
  if (command_id.empty()) {
    return command_rejected(std::move(command_id), {
        {"empty_command_id", "Control command batches must have a command ID."},
    });
  }

  if (commands.empty()) {
    return command_rejected(std::move(command_id), {
        {"empty_command_batch", "Control command batches must contain at least one command."},
    });
  }

  auto next = current_preset_;
  for (const auto& command : commands) {
    auto validation = validate_command(command);
    if (!validation.ok()) {
      return command_rejected(command_id, validation.errors());
    }

    if (!control_command_mutates_preset(command.type)) {
      return command_rejected(command_id, {
          {"unsupported_batch_command",
           "Control command batches currently support preset mutation commands only."},
      });
    }

    auto apply_result = apply_command(next, command);
    if (!apply_result.ok()) {
      return command_rejected(command_id, apply_result.errors());
    }
    next = apply_result.take_document();
  }

  ControlCommand publish_command;
  publish_command.command_id = std::move(command_id);
  publish_command.type = ControlCommandType::LoadPreset;
  return publish_preset(publish_command, std::move(next));
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

const std::vector<platform::VirtualEndpointDescriptor>&
ControlSession::virtual_endpoints() const noexcept {
  return virtual_endpoints_.endpoints();
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
