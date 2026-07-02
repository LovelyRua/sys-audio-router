#include "core/control/control_response.h"

#include <utility>

namespace sar::control {

ControlResponse command_accepted(std::string command_id) {
  ControlResponse response;
  response.command_id = std::move(command_id);
  response.status = ControlResponseStatus::Accepted;
  return response;
}

ControlResponse command_rejected(std::string command_id,
                                 std::vector<PresetError> errors) {
  ControlResponse response;
  response.command_id = std::move(command_id);
  response.status = ControlResponseStatus::Rejected;
  response.errors = std::move(errors);
  return response;
}

ControlResponse diagnostics_response(std::string command_id,
                                     diagnostics::EngineDiagnostics diagnostics) {
  auto response = command_accepted(std::move(command_id));
  response.diagnostics = diagnostics;
  response.has_diagnostics = true;
  return response;
}

}  // namespace sar::control
