#include "core/control/control_command.h"

#include <iostream>
#include <limits>
#include <string>

namespace {

bool has_error_code(const sar::control::ControlCommandValidationResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::control::PresetDocument make_valid_preset() {
  sar::control::PresetDocument preset;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"mic", "Mic"});
  preset.matrix.outputs.push_back({"monitor", "Monitor"});
  preset.matrix.routes.push_back({"mic", "monitor", 1.0F});
  return preset;
}

}  // namespace

int main() {
  {
    sar::control::ControlCommand command;
    command.command_id = "query_1";
    command.type = sar::control::ControlCommandType::QueryDiagnostics;
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(), "Expected query diagnostics command success")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.schema_version = 0;
    command.type = sar::control::ControlCommandType::QueryDiagnostics;
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(!result.ok(), "Expected invalid command failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_schema_version"),
                                    "Expected invalid_schema_version error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_command_id"),
                                    "Expected empty_command_id error")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "route_1";
    command.type = sar::control::ControlCommandType::ConnectRoute;
    command.input_id = "mic";
    command.output_id = "monitor";
    command.gain = 0.75F;
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(), "Expected connect route command success")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "route_bad";
    command.type = sar::control::ControlCommandType::SetGain;
    command.gain = std::numeric_limits<float>::quiet_NaN();
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(!result.ok(), "Expected invalid set gain command failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_route_input"),
                                    "Expected empty_route_input error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_route_output"),
                                    "Expected empty_route_output error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_route_gain"),
                                    "Expected invalid_route_gain error")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "endpoint_1";
    command.type = sar::control::ControlCommandType::CreateVirtualEndpoint;
    command.endpoint_id = "asio_pair_1";
    command.endpoint_label = "ASIO Pair 1";
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(),
                                    "Expected create virtual endpoint command success")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "preset_1";
    command.type = sar::control::ControlCommandType::LoadPreset;
    command.preset = make_valid_preset();
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(), "Expected load preset command success")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "preset_bad";
    command.type = sar::control::ControlCommandType::LoadPreset;
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(!result.ok(), "Expected invalid load preset failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_matrix_inputs"),
                                    "Expected empty_matrix_inputs error")) {
      return failure;
    }
  }

  std::cout << "Control command smoke test passed\n";
  return 0;
}
