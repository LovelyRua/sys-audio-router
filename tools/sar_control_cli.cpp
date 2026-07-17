#include "core/control/control_wire_protocol.h"
#include "core/service/windows_named_pipe_control.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& input) {
  std::vector<std::byte> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = static_cast<std::byte>(input[index]);
  }
  return result;
}

std::vector<std::uint8_t> as_u8(std::span<const std::byte> input) {
  std::vector<std::uint8_t> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = std::to_integer<std::uint8_t>(input[index]);
  }
  return result;
}

void usage() {
  std::cerr << "Usage: sar_control_cli [--pipe NAME] state|diagnostics|graph|"
               "set-gain INPUT OUTPUT VALUE|set-mute INPUT OUTPUT true|false\n";
}

}  // namespace

int main(int argc, char** argv) {
  sar::service::NamedPipeControlConfig pipe_config;
  int index = 1;
  if (index + 1 < argc && std::string{argv[index]} == "--pipe") {
    const std::string name = argv[index + 1];
    pipe_config.pipe_name.assign(name.begin(), name.end());
    index += 2;
  }
  if (index >= argc) {
    usage();
    return 2;
  }

  sar::control::ControlCommand command;
  command.command_id = "cli-1";
  const std::string operation = argv[index++];
  if (operation == "state") {
    command.type = sar::control::ControlCommandType::QuerySessionState;
  } else if (operation == "diagnostics") {
    command.type = sar::control::ControlCommandType::QueryDiagnostics;
  } else if (operation == "graph") {
    command.type = sar::control::ControlCommandType::QueryActiveGraph;
  } else if (operation == "set-gain" && index + 2 < argc) {
    command.type = sar::control::ControlCommandType::SetGain;
    command.input_id = argv[index++];
    command.output_id = argv[index++];
    try {
      command.gain = std::stof(argv[index++]);
    } catch (...) {
      usage();
      return 2;
    }
  } else if (operation == "set-mute" && index + 2 < argc) {
    command.type = sar::control::ControlCommandType::SetMute;
    command.input_id = argv[index++];
    command.output_id = argv[index++];
    const std::string value = argv[index++];
    if (value != "true" && value != "false") {
      usage();
      return 2;
    }
    command.mute = value == "true";
  } else {
    usage();
    return 2;
  }
  if (index != argc) {
    usage();
    return 2;
  }

  const auto request = sar::control::encode_control_command(command);
  if (!request.ok()) {
    std::cerr << "control_request_encode_failed\n";
    return 1;
  }
  const auto transaction = sar::service::transact_named_pipe_control(
      pipe_config, as_bytes(request.bytes));
  if (!transaction.ok()) {
    std::cerr << transaction.error().code << ": "
              << transaction.error().message << '\n';
    return 1;
  }
  const auto response =
      sar::control::decode_control_response(as_u8(transaction.payload()));
  if (!response.ok()) {
    std::cerr << "control_response_decode_failed\n";
    return 1;
  }
  if (response.response.status ==
      sar::control::ControlResponseStatus::Rejected) {
    for (const auto& error : response.response.errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::cout << "control_response status=accepted command_id="
            << response.response.command_id;
  if (response.response.has_active_graph) {
    std::cout << " graph_version=" << response.response.active_graph.version
              << " sample_rate=" << response.response.active_graph.sample_rate
              << " channels=" << response.response.active_graph.channels
              << " frames=" << response.response.active_graph.frames;
  }
  if (response.response.has_session_state) {
    std::cout << " next_graph_version="
              << response.response.next_graph_version
              << " routes=" << response.response.preset.matrix.routes.size();
  }
  if (response.response.has_diagnostics) {
    std::cout << " processed_blocks="
              << response.response.diagnostics.processed_blocks
              << " xruns=" << response.response.diagnostics.xrun_count
              << " capture_fifo_frames="
              << response.response.diagnostics.capture_fifo_fill_frames
              << " render_fifo_frames="
              << response.response.diagnostics.render_fifo_fill_frames
              << " capture_overflow_frames="
              << response.response.diagnostics.capture_fifo_overflow_frames
              << " render_overflow_frames="
              << response.response.diagnostics.render_fifo_overflow_frames
              << " render_underflow_frames="
              << response.response.diagnostics.render_fifo_underflow_frames
              << " callback_peak_us="
              << response.response.diagnostics.peak_callback_seconds * 1'000'000.0;
  }
  std::cout << '\n';
  return 0;
}
