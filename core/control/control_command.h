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
  QuerySessionState,
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

class ControlApplyResult {
 public:
  static ControlApplyResult success(PresetDocument document);
  static ControlApplyResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const PresetDocument& document() const noexcept;
  [[nodiscard]] PresetDocument take_document() noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  ControlApplyResult(PresetDocument document, std::vector<PresetError> errors);

  PresetDocument document_;
  std::vector<PresetError> errors_;
};

[[nodiscard]] ControlCommandValidationResult validate_command(const ControlCommand& command);
[[nodiscard]] ControlApplyResult apply_command(const PresetDocument& current,
                                               const ControlCommand& command);

}  // namespace sar::control
