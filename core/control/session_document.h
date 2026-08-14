#pragma once

#include "core/control/control_command.h"
#include "core/control/preset_document.h"
#include "core/control/virtual_asio_device_definition.h"

#include <cstdint>
#include <vector>

namespace sar::control {

inline constexpr std::uint32_t kSessionDocumentSchemaVersion = 2;
[[nodiscard]] VirtualAsioDeviceDefinition
default_virtual_asio_device_definition();

struct SessionDocument {
  std::uint32_t schema_version = kSessionDocumentSchemaVersion;
  PresetDocument preset;
  AudioRuntimeConfiguration audio_runtime;
  std::vector<VirtualAsioDeviceDefinition> virtual_asio_devices;
  bool auto_start = false;
};

class SessionDocumentValidationResult {
 public:
  static SessionDocumentValidationResult success();
  static SessionDocumentValidationResult failure(
      std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  explicit SessionDocumentValidationResult(std::vector<PresetError> errors);

  std::vector<PresetError> errors_;
};

[[nodiscard]] SessionDocumentValidationResult validate_session_document(
    const SessionDocument& session);

}  // namespace sar::control
