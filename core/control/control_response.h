#pragma once

#include "core/control/preset_document.h"
#include "core/diagnostics/engine_diagnostics.h"

#include <string>
#include <vector>

namespace sar::control {

enum class ControlResponseStatus {
  Accepted,
  Rejected,
};

struct ControlResponse {
  std::string command_id;
  ControlResponseStatus status = ControlResponseStatus::Accepted;
  std::vector<PresetError> errors;
  diagnostics::EngineDiagnostics diagnostics;
  bool has_diagnostics = false;
};

[[nodiscard]] ControlResponse command_accepted(std::string command_id);
[[nodiscard]] ControlResponse command_rejected(std::string command_id,
                                               std::vector<PresetError> errors);
[[nodiscard]] ControlResponse diagnostics_response(
    std::string command_id,
    diagnostics::EngineDiagnostics diagnostics);

}  // namespace sar::control
