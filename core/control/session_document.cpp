#include "core/control/session_document.h"

#include "core/control/control_wire_protocol.h"
#include "core/platform/virtual_asio_client_registry.h"

#include <iterator>
#include <unordered_set>
#include <utility>

namespace sar::control {

VirtualAsioDeviceDefinition default_virtual_asio_device_definition() {
  return {
      .device_id = "default",
      .clsid = "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}",
      .registry_name = "System Audio Route",
      .broker_token = "virtual-asio",
      .input_channels = 2,
      .output_channels = 2,
      .enabled = true,
  };
}

SessionDocumentValidationResult SessionDocumentValidationResult::success() {
  return SessionDocumentValidationResult({});
}

SessionDocumentValidationResult SessionDocumentValidationResult::failure(
    std::vector<PresetError> errors) {
  return SessionDocumentValidationResult(std::move(errors));
}

bool SessionDocumentValidationResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<PresetError>& SessionDocumentValidationResult::errors()
    const noexcept {
  return errors_;
}

SessionDocumentValidationResult::SessionDocumentValidationResult(
    std::vector<PresetError> errors)
    : errors_(std::move(errors)) {}

SessionDocumentValidationResult validate_session_document(
    const SessionDocument& session) {
  std::vector<PresetError> errors;

  if (session.schema_version != kSessionDocumentSchemaVersion) {
    errors.push_back({
        "unsupported_session_schema_version",
        "Session document schema version is not supported.",
    });
  }

  const auto preset_validation = validate_preset(session.preset);
  for (const auto& error : preset_validation.errors()) {
    errors.push_back(error);
  }

  const auto& runtime = session.audio_runtime;
  if (runtime.capture_device_id.size() > kControlWireMaxStringBytes ||
      runtime.render_device_id.size() > kControlWireMaxStringBytes) {
    errors.push_back({
        "audio_runtime_device_id_too_long",
        "Audio runtime device IDs exceed the supported length.",
    });
  }
  for (const auto& endpoint : runtime.endpoints) {
    if (endpoint.endpoint_id.size() > kControlWireMaxStringBytes ||
        endpoint.device_id.size() > kControlWireMaxStringBytes) {
      errors.push_back({
          "audio_runtime_endpoint_field_too_long",
          "Audio runtime endpoint fields exceed the supported length.",
      });
    }
  }

  auto runtime_errors = validate_audio_runtime_configuration(runtime, true);
  errors.insert(errors.end(),
                std::make_move_iterator(runtime_errors.begin()),
                std::make_move_iterator(runtime_errors.end()));

  if (session.virtual_asio_devices.empty() ||
      session.virtual_asio_devices.size() > kMaximumVirtualAsioDevices) {
    errors.push_back({
        "invalid_virtual_asio_device_count",
        "Session requires between one and sixteen Virtual ASIO devices.",
    });
  }
  std::unordered_set<std::string> device_ids;
  std::unordered_set<std::string> clsids;
  std::unordered_set<std::string> registry_names;
  std::unordered_set<std::string> broker_tokens;
  for (const auto& device : session.virtual_asio_devices) {
    const bool empty_field = device.device_id.empty() || device.clsid.empty() ||
                             device.registry_name.empty() ||
                             device.broker_token.empty();
    const bool oversized =
        device.device_id.size() > kControlWireMaxStringBytes ||
        device.clsid.size() > kControlWireMaxStringBytes ||
        device.registry_name.size() > kControlWireMaxStringBytes ||
        device.broker_token.size() > kControlWireMaxStringBytes;
    if (empty_field || oversized) {
      errors.push_back({
          "invalid_virtual_asio_device_identity",
          "Virtual ASIO device identity fields must be non-empty and bounded.",
      });
    }
    if (device.input_channels == 0 || device.output_channels == 0 ||
        device.input_channels > platform::kVirtualAsioMaxChannels ||
        device.output_channels > platform::kVirtualAsioMaxChannels) {
      errors.push_back({
          "invalid_virtual_asio_device_channels",
          "Virtual ASIO channel counts must be between one and sixty-four.",
      });
    }
    if (!device_ids.insert(device.device_id).second ||
        !clsids.insert(device.clsid).second ||
        !registry_names.insert(device.registry_name).second ||
        !broker_tokens.insert(device.broker_token).second) {
      errors.push_back({
          "duplicate_virtual_asio_device_identity",
          "Virtual ASIO device IDs, CLSIDs, registry names, and broker tokens "
          "must be unique.",
      });
    }
  }

  if (session.auto_start && runtime.mode == AudioRuntimeMode::None) {
    errors.push_back({
        "auto_start_without_audio_runtime",
        "Session auto-start requires a configured audio runtime.",
    });
  }

  if (!errors.empty()) {
    return SessionDocumentValidationResult::failure(std::move(errors));
  }
  return SessionDocumentValidationResult::success();
}

}  // namespace sar::control
