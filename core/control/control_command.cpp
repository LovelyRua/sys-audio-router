#include "core/control/control_command.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sar::control {

namespace {

void require_non_empty(const std::string& value,
                       const char* code,
                       const char* message,
                       std::vector<PresetError>& errors) {
  if (value.empty()) {
    errors.push_back({code, message});
  }
}

void validate_route_binding(const ControlCommand& command,
                            std::vector<PresetError>& errors) {
  require_non_empty(command.input_id,
                    "empty_route_input",
                    "Route commands must reference an input endpoint.",
                    errors);
  require_non_empty(command.output_id,
                    "empty_route_output",
                    "Route commands must reference an output endpoint.",
                    errors);
}

auto find_route(std::vector<PresetRoute>& routes,
                const std::string& input_id,
                const std::string& output_id) {
  return std::ranges::find_if(routes, [&](const PresetRoute& route) {
    return route.input_id == input_id && route.output_id == output_id;
  });
}

}  // namespace

ControlCommandValidationResult ControlCommandValidationResult::success() {
  return ControlCommandValidationResult({});
}

ControlCommandValidationResult ControlCommandValidationResult::failure(
    std::vector<PresetError> errors) {
  return ControlCommandValidationResult(std::move(errors));
}

bool ControlCommandValidationResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<PresetError>& ControlCommandValidationResult::errors() const noexcept {
  return errors_;
}

ControlCommandValidationResult::ControlCommandValidationResult(
    std::vector<PresetError> errors)
    : errors_(std::move(errors)) {}

ControlCommandValidationResult validate_command(const ControlCommand& command) {
  std::vector<PresetError> errors;

  if (command.schema_version == 0) {
    errors.push_back({"invalid_schema_version", "Command schema version must be non-zero."});
  }

  require_non_empty(command.command_id,
                    "empty_command_id",
                    "Control commands must have a command ID.",
                    errors);

  switch (command.type) {
    case ControlCommandType::ListDevices:
    case ControlCommandType::SavePreset:
    case ControlCommandType::QueryDiagnostics:
    case ControlCommandType::QueryActiveGraph:
      break;

    case ControlCommandType::CreateVirtualEndpoint:
      require_non_empty(command.endpoint_id,
                        "empty_endpoint_id",
                        "CreateVirtualEndpoint requires an endpoint ID.",
                        errors);
      require_non_empty(command.endpoint_label,
                        "empty_endpoint_label",
                        "CreateVirtualEndpoint requires an endpoint label.",
                        errors);
      break;

    case ControlCommandType::RemoveVirtualEndpoint:
      require_non_empty(command.endpoint_id,
                        "empty_endpoint_id",
                        "RemoveVirtualEndpoint requires an endpoint ID.",
                        errors);
      break;

    case ControlCommandType::ConnectRoute:
    case ControlCommandType::SetGain:
      validate_route_binding(command, errors);
      if (!std::isfinite(command.gain)) {
        errors.push_back({"invalid_route_gain", "Route gain must be finite."});
      }
      break;

    case ControlCommandType::DisconnectRoute:
    case ControlCommandType::SetMute:
      validate_route_binding(command, errors);
      break;

    case ControlCommandType::LoadPreset: {
      const auto preset_result = validate_preset(command.preset);
      for (const auto& error : preset_result.errors()) {
        errors.push_back(error);
      }
      break;
    }
  }

  if (!errors.empty()) {
    return ControlCommandValidationResult::failure(std::move(errors));
  }
  return ControlCommandValidationResult::success();
}

ControlApplyResult ControlApplyResult::success(PresetDocument document) {
  return {std::move(document), {}};
}

ControlApplyResult ControlApplyResult::failure(std::vector<PresetError> errors) {
  return {{}, std::move(errors)};
}

bool ControlApplyResult::ok() const noexcept {
  return errors_.empty();
}

const PresetDocument& ControlApplyResult::document() const noexcept {
  return document_;
}

PresetDocument ControlApplyResult::take_document() noexcept {
  return std::move(document_);
}

const std::vector<PresetError>& ControlApplyResult::errors() const noexcept {
  return errors_;
}

ControlApplyResult::ControlApplyResult(PresetDocument document,
                                       std::vector<PresetError> errors)
    : document_(std::move(document)), errors_(std::move(errors)) {}

ControlApplyResult apply_command(const PresetDocument& current,
                                 const ControlCommand& command) {
  auto command_validation = validate_command(command);
  if (!command_validation.ok()) {
    return ControlApplyResult::failure(command_validation.errors());
  }

  if (command.type == ControlCommandType::LoadPreset) {
    return ControlApplyResult::success(command.preset);
  }

  auto next = current;
  auto preset_validation = validate_preset(next);
  if (!preset_validation.ok()) {
    return ControlApplyResult::failure(preset_validation.errors());
  }

  switch (command.type) {
    case ControlCommandType::ConnectRoute: {
      auto route = find_route(next.matrix.routes, command.input_id, command.output_id);
      if (route != next.matrix.routes.end()) {
        return ControlApplyResult::failure({
            {"duplicate_route", "ConnectRoute cannot redefine an existing route."},
        });
      }
      next.matrix.routes.push_back({
          command.input_id,
          command.output_id,
          command.gain,
          false,
      });
      break;
    }

    case ControlCommandType::DisconnectRoute: {
      auto route = find_route(next.matrix.routes, command.input_id, command.output_id);
      if (route == next.matrix.routes.end()) {
        return ControlApplyResult::failure({
            {"unknown_route", "DisconnectRoute references an unknown route."},
        });
      }
      next.matrix.routes.erase(route);
      break;
    }

    case ControlCommandType::SetGain: {
      auto route = find_route(next.matrix.routes, command.input_id, command.output_id);
      if (route == next.matrix.routes.end()) {
        return ControlApplyResult::failure({
            {"unknown_route", "SetGain references an unknown route."},
        });
      }
      route->gain = command.gain;
      break;
    }

    case ControlCommandType::SetMute: {
      auto route = find_route(next.matrix.routes, command.input_id, command.output_id);
      if (route == next.matrix.routes.end()) {
        return ControlApplyResult::failure({
            {"unknown_route", "SetMute references an unknown route."},
        });
      }
      route->muted = command.mute;
      break;
    }

    case ControlCommandType::ListDevices:
    case ControlCommandType::CreateVirtualEndpoint:
    case ControlCommandType::RemoveVirtualEndpoint:
    case ControlCommandType::SavePreset:
    case ControlCommandType::QueryDiagnostics:
    case ControlCommandType::QueryActiveGraph:
    case ControlCommandType::LoadPreset:
      break;
  }

  return ControlApplyResult::success(std::move(next));
}

}  // namespace sar::control
