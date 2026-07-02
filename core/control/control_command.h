#pragma once

#include "core/control/preset_document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sar::control {

enum class ControlCommandType {
  ListDevices,
  CreateVirtualEndpoint,
  RemoveVirtualEndpoint,
  ConnectRoute,
  DisconnectRoute,
  SetGain,
  SetMute,
  LoadPreset,
  SavePreset,
  QueryDiagnostics,
  QueryActiveGraph,
};

struct ControlCommand {
  std::uint32_t schema_version = 1;
  std::string command_id;
  ControlCommandType type = ControlCommandType::QueryDiagnostics;

  std::string endpoint_id;
  std::string endpoint_label;
  std::string input_id;
  std::string output_id;
  float gain = 0.0F;
  bool mute = false;
  PresetDocument preset;
};

class ControlCommandValidationResult {
 public:
  static ControlCommandValidationResult success();
  static ControlCommandValidationResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  explicit ControlCommandValidationResult(std::vector<PresetError> errors);

  std::vector<PresetError> errors_;
};

[[nodiscard]] ControlCommandValidationResult validate_command(const ControlCommand& command);

}  // namespace sar::control
