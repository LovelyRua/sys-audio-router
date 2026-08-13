#include "core/control/control_command.h"

#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

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

bool has_error_code(const sar::control::ControlApplyResult& result,
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
    command.command_id = "state_1";
    command.type = sar::control::ControlCommandType::QuerySessionState;
    const auto result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(), "Expected query session state success")) {
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

  {
    auto preset = make_valid_preset();

    sar::control::ControlCommand set_gain;
    set_gain.command_id = "gain_1";
    set_gain.type = sar::control::ControlCommandType::SetGain;
    set_gain.input_id = "mic";
    set_gain.output_id = "monitor";
    set_gain.gain = 0.5F;
    auto gain_result = sar::control::apply_command(preset, set_gain);
    if (const auto failure = expect(gain_result.ok(), "Expected set gain apply success")) {
      return failure;
    }
    preset = gain_result.take_document();
    if (const auto failure = expect(preset.matrix.routes[0].gain == 0.5F,
                                    "Expected route gain update")) {
      return failure;
    }

    sar::control::ControlCommand mute;
    mute.command_id = "mute_1";
    mute.type = sar::control::ControlCommandType::SetMute;
    mute.input_id = "mic";
    mute.output_id = "monitor";
    mute.mute = true;
    auto mute_result = sar::control::apply_command(preset, mute);
    if (const auto failure = expect(mute_result.ok(), "Expected mute apply success")) {
      return failure;
    }
    preset = mute_result.take_document();
    if (const auto failure = expect(preset.matrix.routes[0].muted,
                                    "Expected route muted state update")) {
      return failure;
    }

    auto matrix_result = sar::control::build_route_matrix(preset);
    if (const auto failure = expect(matrix_result.ok(), "Expected muted matrix build success")) {
      return failure;
    }
    auto matrix = matrix_result.take_matrix();
    sar::realtime::AudioBuffer input(1, 1);
    sar::realtime::AudioBuffer output(1, 1);
    input.channel(0)[0] = 1.0F;
    matrix->process(input, output);
    if (!sar::tests::nearly_equal(output.channel(0)[0], 0.0F)) {
      std::cerr << "Expected muted route to produce silence\n";
      return 1;
    }
  }

  {
    auto preset = make_valid_preset();

    sar::control::ControlCommand disconnect;
    disconnect.command_id = "disconnect_1";
    disconnect.type = sar::control::ControlCommandType::DisconnectRoute;
    disconnect.input_id = "mic";
    disconnect.output_id = "monitor";
    auto disconnect_result = sar::control::apply_command(preset, disconnect);
    if (const auto failure = expect(disconnect_result.ok(),
                                    "Expected disconnect apply success")) {
      return failure;
    }
    preset = disconnect_result.take_document();
    if (const auto failure = expect(preset.matrix.routes.empty(),
                                    "Expected route to be disconnected")) {
      return failure;
    }

    sar::control::ControlCommand connect;
    connect.command_id = "connect_2";
    connect.type = sar::control::ControlCommandType::ConnectRoute;
    connect.input_id = "mic";
    connect.output_id = "monitor";
    connect.gain = 0.25F;
    auto connect_result = sar::control::apply_command(preset, connect);
    if (const auto failure = expect(connect_result.ok(), "Expected connect apply success")) {
      return failure;
    }
    preset = connect_result.take_document();
    if (const auto failure = expect(preset.matrix.routes.size() == 1,
                                    "Expected route to be connected")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    sar::control::ControlCommand command;
    command.command_id = "connect_duplicate";
    command.type = sar::control::ControlCommandType::ConnectRoute;
    command.input_id = "mic";
    command.output_id = "monitor";
    command.gain = 1.0F;
    const auto result = sar::control::apply_command(preset, command);
    if (const auto failure = expect(!result.ok(), "Expected duplicate connect failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_route"),
                                    "Expected duplicate_route error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    sar::control::ControlCommand command;
    command.command_id = "connect_unknown_input";
    command.type = sar::control::ControlCommandType::ConnectRoute;
    command.input_id = "missing";
    command.output_id = "monitor";
    command.gain = 1.0F;
    const auto result = sar::control::apply_command(preset, command);
    if (const auto failure = expect(!result.ok(), "Expected unknown input connect failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unknown_route_input"),
                                    "Expected unknown_route_input error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    sar::control::ControlCommand command;
    command.command_id = "connect_unknown_output";
    command.type = sar::control::ControlCommandType::ConnectRoute;
    command.input_id = "mic";
    command.output_id = "missing";
    command.gain = 1.0F;
    const auto result = sar::control::apply_command(preset, command);
    if (const auto failure = expect(!result.ok(), "Expected unknown output connect failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unknown_route_output"),
                                    "Expected unknown_route_output error")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand command;
    command.command_id = "configure_runtime";
    command.type =
        sar::control::ControlCommandType::ConfigureAudioRuntime;
    auto result = sar::control::validate_command(command);
    if (const auto failure = expect(
            has_error_code(result, "missing_audio_runtime_mode"),
            "Expected missing runtime mode validation error")) {
      return failure;
    }

    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiDuplex;
    command.audio_runtime.capture_device_id = "capture-only";
    result = sar::control::validate_command(command);
    if (const auto failure = expect(
            has_error_code(result, "incomplete_duplex_device_ids"),
            "Expected incomplete duplex IDs validation error")) {
      return failure;
    }

    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiRender;
    result = sar::control::validate_command(command);
    if (const auto failure = expect(
            has_error_code(result, "unexpected_capture_device_id"),
            "Expected render capture ID validation error")) {
      return failure;
    }

    command.audio_runtime.capture_device_id.clear();
    command.audio_runtime.render_device_id = "render-1";
    result = sar::control::validate_command(command);
    if (const auto failure = expect(result.ok(),
                                    "Expected pinned render configuration success")) {
      return failure;
    }

    command.audio_runtime = {};
    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiMatrix;
    command.audio_runtime.endpoints = {
        {"capture-a", "native-capture-a",
         sar::control::AudioRuntimeEndpointDirection::Capture, false, 0, 2},
        {"render-main", "native-render-a",
         sar::control::AudioRuntimeEndpointDirection::Render, true, 0, 2},
        {"render-b", "native-render-b",
         sar::control::AudioRuntimeEndpointDirection::Render, false, 0, 2},
    };
    result = sar::control::validate_command(command);
    if (const auto failure = expect(
            result.ok(), "Expected valid multi-endpoint matrix configuration")) {
      return failure;
    }

    command.audio_runtime.endpoints[0].endpoint_id = "render-main";
    command.audio_runtime.endpoints[1].clock_master = false;
    result = sar::control::validate_command(command);
    if (const auto failure = expect(
            has_error_code(result, "duplicate_audio_runtime_endpoint_id"),
            "Expected duplicate logical endpoint validation error")) {
      return failure;
    }
    if (const auto failure = expect(
            has_error_code(result,
                           "invalid_audio_runtime_clock_master_count"),
            "Expected missing matrix clock master validation error")) {
      return failure;
    }

    command.audio_runtime.endpoints[0].endpoint_id = "capture-a";
    command.audio_runtime.endpoints[0].clock_master = true;
    result = sar::control::validate_command(command);
    if (const auto failure = expect(
            has_error_code(result, "capture_clock_master_not_supported"),
            "Expected capture clock master validation error")) {
      return failure;
    }
  }

  std::cout << "Control command smoke test passed\n";
  return 0;
}
