#include "core/control/control_command.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <unordered_set>
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

std::vector<PresetError> validate_audio_runtime_configuration(
    const AudioRuntimeConfiguration& configuration,
    bool allow_none) {
  std::vector<PresetError> errors;

  const auto has_physical_asio_fields = [&] {
    return !configuration.physical_asio_driver_clsid.empty() ||
           configuration.physical_asio_sample_rate != 0 ||
           configuration.physical_asio_block_frames != 0 ||
           !configuration.physical_asio_input_channels.empty() ||
           !configuration.physical_asio_output_channels.empty();
  };

  if (configuration.mode == AudioRuntimeMode::PhysicalAsio) {
    if (!configuration.capture_device_id.empty() ||
        !configuration.render_device_id.empty() ||
        !configuration.endpoints.empty()) {
      errors.push_back({"unexpected_wasapi_configuration",
                        "Physical ASIO mode does not accept WASAPI devices or endpoints."});
    }
    if (configuration.physical_asio_driver_clsid.empty()) {
      errors.push_back({"empty_physical_asio_driver_clsid",
                        "Physical ASIO mode requires a driver CLSID."});
    } else if (configuration.physical_asio_driver_clsid.size() > 128) {
      errors.push_back({"physical_asio_driver_clsid_too_long",
                        "Physical ASIO driver CLSID exceeds the supported length."});
    }
    if (configuration.physical_asio_sample_rate < 8000 ||
        configuration.physical_asio_sample_rate > 768000) {
      errors.push_back({"invalid_physical_asio_sample_rate",
                        "Physical ASIO sample rate must be between 8000 and 768000 Hz."});
    }
    if (configuration.physical_asio_block_frames == 0 ||
        configuration.physical_asio_block_frames > 65536) {
      errors.push_back({"invalid_physical_asio_block_frames",
                        "Physical ASIO block size must be between 1 and 65536 frames."});
    }
    const auto& inputs = configuration.physical_asio_input_channels;
    const auto& outputs = configuration.physical_asio_output_channels;
    // Empty input and output lists select all native driver channels. This
    // allows routine device enumeration to remain registry-only.
    if (inputs.size() > kMaximumPhysicalAsioChannels ||
        outputs.size() > kMaximumPhysicalAsioChannels) {
      errors.push_back({"too_many_physical_asio_channels",
                        "Physical ASIO channel selection exceeds the supported capacity."});
    }
    const auto validate_channels = [&](const std::vector<std::uint32_t>& channels,
                                       const char* duplicate_code) {
      std::unordered_set<std::uint32_t> unique;
      for (const auto channel : channels) {
        if (channel >= kMaximumPhysicalAsioChannels) {
          errors.push_back({"physical_asio_channel_out_of_range",
                            "Physical ASIO channel index exceeds the supported range."});
        } else if (!unique.insert(channel).second) {
          errors.push_back({duplicate_code,
                            "Physical ASIO channel selections must be unique."});
        }
      }
    };
    validate_channels(inputs, "duplicate_physical_asio_input_channel");
    validate_channels(outputs, "duplicate_physical_asio_output_channel");
    return errors;
  }

  if (configuration.mode == AudioRuntimeMode::None) {
    if (!allow_none) {
      errors.push_back({"missing_audio_runtime_mode",
                        "ConfigureAudioRuntime requires a runtime mode."});
    }
    if (!configuration.capture_device_id.empty() ||
        !configuration.render_device_id.empty() ||
        !configuration.endpoints.empty() || has_physical_asio_fields()) {
      errors.push_back({"unexpected_audio_runtime_endpoint",
                        "A disabled audio runtime cannot specify endpoints."});
    }
    return errors;
  }

  if (has_physical_asio_fields()) {
    errors.push_back({"unexpected_physical_asio_configuration",
                      "WASAPI modes do not accept Physical ASIO configuration."});
  }

  if (configuration.mode == AudioRuntimeMode::WasapiRender) {
    if (!configuration.capture_device_id.empty()) {
      errors.push_back({
          "unexpected_capture_device_id",
          "WASAPI render configuration does not accept a capture device ID.",
      });
    }
    if (!configuration.endpoints.empty()) {
      errors.push_back({"unexpected_matrix_endpoints",
                        "Legacy WASAPI modes do not accept matrix endpoints."});
    }
    return errors;
  }

  if (configuration.mode == AudioRuntimeMode::WasapiDuplex) {
    if (configuration.capture_device_id.empty() !=
        configuration.render_device_id.empty()) {
      errors.push_back({
          "incomplete_duplex_device_ids",
          "WASAPI duplex configuration requires both device IDs or neither.",
      });
    }
    if (!configuration.endpoints.empty()) {
      errors.push_back({"unexpected_matrix_endpoints",
                        "Legacy WASAPI modes do not accept matrix endpoints."});
    }
    return errors;
  }

  if (configuration.mode != AudioRuntimeMode::WasapiMatrix) {
    errors.push_back(
        {"invalid_audio_runtime_mode", "Audio runtime mode is not supported."});
    return errors;
  }

  if (!configuration.capture_device_id.empty() ||
      !configuration.render_device_id.empty()) {
    errors.push_back({
        "unexpected_legacy_device_id",
        "WASAPI matrix mode uses endpoint descriptors, not legacy device IDs.",
    });
  }
  if (configuration.endpoints.empty()) {
    errors.push_back({"empty_audio_runtime_matrix",
                      "WASAPI matrix mode requires at least one endpoint."});
    return errors;
  }
  if (configuration.endpoints.size() > kMaximumAudioRuntimeEndpoints) {
    errors.push_back({"too_many_audio_runtime_endpoints",
                      "WASAPI matrix mode exceeds the endpoint capacity."});
    return errors;
  }

  std::unordered_set<std::string> endpoint_ids;
  std::unordered_set<std::string> native_endpoints;
  std::size_t render_count = 0;
  std::size_t clock_master_count = 0;
  for (const auto& endpoint : configuration.endpoints) {
    if (endpoint.endpoint_id.empty()) {
      errors.push_back({"empty_audio_runtime_endpoint_id",
                        "Matrix endpoints require a stable endpoint ID."});
    } else if (!endpoint_ids.insert(endpoint.endpoint_id).second) {
      errors.push_back({"duplicate_audio_runtime_endpoint_id",
                        "Matrix endpoint IDs must be unique."});
    }

    const bool render =
        endpoint.direction == AudioRuntimeEndpointDirection::Render;
    if (render) {
      ++render_count;
    } else if (endpoint.direction != AudioRuntimeEndpointDirection::Capture) {
      errors.push_back({"invalid_audio_runtime_endpoint_direction",
                        "Matrix endpoint direction is invalid."});
    }

    const bool physical_asio =
        endpoint.backend == AudioRuntimeEndpointBackend::PhysicalAsio;
    if (endpoint.backend != AudioRuntimeEndpointBackend::Wasapi &&
        !physical_asio) {
      errors.push_back({"invalid_audio_runtime_endpoint_backend",
                        "Matrix endpoint backend is invalid."});
    } else if (physical_asio) {
      if (endpoint.device_group_id.empty()) {
        errors.push_back({"empty_physical_asio_device_group_id",
                          "Physical ASIO endpoints require a device group ID."});
      }
      if (endpoint.sample_rate < 8000 || endpoint.sample_rate > 768000) {
        errors.push_back({"invalid_physical_asio_endpoint_sample_rate",
                          "Physical ASIO endpoint sample rate must be between 8000 and 768000 Hz."});
      }
      if (endpoint.block_frames == 0 || endpoint.block_frames > 65536) {
        errors.push_back({"invalid_physical_asio_endpoint_block_frames",
                          "Physical ASIO endpoint block size must be between 1 and 65536 frames."});
      }
    } else if (!endpoint.device_group_id.empty() || endpoint.sample_rate != 0 ||
               endpoint.block_frames != 0) {
      errors.push_back({"unexpected_wasapi_endpoint_timing",
                        "WASAPI endpoints do not accept Physical ASIO group or timing fields."});
    }

    std::string native_key(
        physical_asio ? "physical-asio\n" : "wasapi\n");
    native_key += render ? "render\n" : "capture\n";
    native_key += physical_asio ? endpoint.device_group_id
                                : endpoint.device_id;
    if (!native_endpoints.insert(std::move(native_key)).second) {
      errors.push_back({
          "duplicate_audio_runtime_device",
          "A native device direction can appear only once in a matrix runtime.",
      });
    }

    if (endpoint.clock_master) {
      ++clock_master_count;
      if (!render) {
        errors.push_back({"capture_clock_master_not_supported",
                          "The matrix clock master must be a render endpoint."});
      }
    }
    if (endpoint.channel_count == 0) {
      errors.push_back({"empty_audio_runtime_channel_range",
                        "Matrix endpoints require an explicit channel count."});
    } else if (endpoint.first_channel >
               std::numeric_limits<std::uint32_t>::max() -
                   endpoint.channel_count) {
      errors.push_back({"invalid_audio_runtime_channel_range",
                        "Matrix endpoint channel range overflows."});
    }
  }
  if (render_count == 0) {
    errors.push_back({
        "missing_audio_runtime_render_endpoint",
        "WASAPI matrix mode requires a render endpoint to drive the graph.",
    });
  }
  if (clock_master_count != 1) {
    errors.push_back({"invalid_audio_runtime_clock_master_count",
                      "WASAPI matrix mode requires exactly one clock master."});
  }
  return errors;
}

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
    case ControlCommandType::QuerySessionState:
    case ControlCommandType::QueryAudioRuntime:
    case ControlCommandType::StartAudioRuntime:
    case ControlCommandType::StopAudioRuntime:
    case ControlCommandType::QueryVirtualAsioDevices:
      break;

    case ControlCommandType::ConfigureVirtualAsioDevices:
      if (command.virtual_asio_devices.empty() ||
          command.virtual_asio_devices.size() > kMaximumVirtualAsioDevices) {
        errors.push_back({
            "invalid_virtual_asio_device_count",
            "ConfigureVirtualAsioDevices requires between one and sixteen devices.",
        });
      }
      break;

    case ControlCommandType::ConfigureAudioRuntime: {
      auto runtime_errors =
          validate_audio_runtime_configuration(command.audio_runtime, false);
      errors.insert(errors.end(),
                    std::make_move_iterator(runtime_errors.begin()),
                    std::make_move_iterator(runtime_errors.end()));
      break;
    }

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

    default:
      errors.push_back({"unknown_command_type",
                        "Control command type is not supported."});
      break;
  }

  if (!errors.empty()) {
    return ControlCommandValidationResult::failure(std::move(errors));
  }
  return ControlCommandValidationResult::success();
}

bool control_command_mutates_preset(ControlCommandType type) noexcept {
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
    case ControlCommandType::QuerySessionState:
    case ControlCommandType::QueryAudioRuntime:
    case ControlCommandType::StartAudioRuntime:
    case ControlCommandType::StopAudioRuntime:
    case ControlCommandType::ConfigureAudioRuntime:
    case ControlCommandType::QueryVirtualAsioDevices:
    case ControlCommandType::ConfigureVirtualAsioDevices:
      return false;
  }

  return false;
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
    case ControlCommandType::QuerySessionState:
    case ControlCommandType::QueryAudioRuntime:
    case ControlCommandType::StartAudioRuntime:
    case ControlCommandType::StopAudioRuntime:
    case ControlCommandType::ConfigureAudioRuntime:
    case ControlCommandType::QueryVirtualAsioDevices:
    case ControlCommandType::ConfigureVirtualAsioDevices:
    case ControlCommandType::LoadPreset:
      break;
  }

  auto next_validation = validate_preset(next);
  if (!next_validation.ok()) {
    return ControlApplyResult::failure(next_validation.errors());
  }

  return ControlApplyResult::success(std::move(next));
}

}  // namespace sar::control
