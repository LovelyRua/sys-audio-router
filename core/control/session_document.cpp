#include "core/control/session_document.h"

#include "core/control/control_wire_protocol.h"

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

  switch (runtime.mode) {
    case AudioRuntimeMode::None:
      if (!runtime.capture_device_id.empty() ||
          !runtime.render_device_id.empty()) {
        errors.push_back({
            "unexpected_audio_runtime_device_id",
            "A disabled audio runtime cannot specify device IDs.",
        });
      }
      break;

    case AudioRuntimeMode::WasapiRender:
      if (!runtime.capture_device_id.empty()) {
        errors.push_back({
            "unexpected_capture_device_id",
            "WASAPI render configuration does not accept a capture device ID.",
        });
      }
      break;

    case AudioRuntimeMode::WasapiDuplex:
      if (runtime.capture_device_id.empty() !=
          runtime.render_device_id.empty()) {
        errors.push_back({
            "incomplete_duplex_device_ids",
            "WASAPI duplex configuration requires both device IDs or neither.",
        });
      }
      break;

    default:
      errors.push_back({
          "invalid_audio_runtime_mode",
          "Session document contains an invalid audio runtime mode.",
      });
      break;
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
