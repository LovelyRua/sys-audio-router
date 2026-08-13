#include "core/control/session_document.h"

#include "core/control/control_wire_protocol.h"

#include <iterator>
#include <utility>

namespace sar::control {

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
