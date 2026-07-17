#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic_bool stop_requested = false;

BOOL WINAPI console_handler(DWORD signal) {
  if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT ||
      signal == CTRL_CLOSE_EVENT || signal == CTRL_SHUTDOWN_EVENT) {
    stop_requested.store(true);
    return TRUE;
  }
  return FALSE;
}

sar::control::PresetDocument initial_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input-l", "Input L"});
  preset.matrix.inputs.push_back({"input-r", "Input R"});
  preset.matrix.outputs.push_back({"output-l", "Output L"});
  preset.matrix.outputs.push_back({"output-r", "Output R"});
  preset.matrix.routes.push_back({"input-l", "output-l", 1.0F, false});
  preset.matrix.routes.push_back({"input-r", "output-r", 1.0F, false});
  return preset;
}

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

}  // namespace

int main(int argc, char** argv) {
  sar::service::NamedPipeControlConfig pipe_config;
  std::string pipe_display_name = "sys-audio-route-control";
  bool once = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--once") {
      once = true;
    } else if (argument == "--pipe" && index + 1 < argc) {
      const std::string name = argv[++index];
      pipe_config.pipe_name.assign(name.begin(), name.end());
      pipe_display_name = name;
    } else {
      std::cerr << "Usage: sar_engine_service [--pipe NAME] [--once]\n";
      return 2;
    }
  }

  auto service_result =
      sar::service::EngineControlService::create(initial_preset());
  if (!service_result.ok()) {
    for (const auto& error : service_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }
  auto service = service_result.take_service();
  sar::service::WindowsNamedPipeControlServer pipe_server(
      pipe_config,
      [&service](std::span<const std::byte> request) {
        const auto response = service->handle_wire_request(as_u8(request));
        if (!response.ok()) {
          return sar::service::NamedPipeControlResult::failure(
              {"control_response_encode_failed",
               "Engine control response could not be encoded.", 0});
        }
        return sar::service::NamedPipeControlResult::success(
            as_bytes(response.bytes));
      });
  const auto started = pipe_server.start();
  if (!started.ok()) {
    std::cerr << started.error().code << ": " << started.error().message << '\n';
    return 1;
  }

  SetConsoleCtrlHandler(console_handler, TRUE);
  std::cout << "engine_service_state=running pipe=" << pipe_display_name << '\n';
  while (!stop_requested.load()) {
    if (once && pipe_server.stats().completed_requests >= 1) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  pipe_server.stop();
  const auto stats = pipe_server.stats();
  std::cout << "engine_service_state=stopped requests="
            << stats.completed_requests << " protocol_errors="
            << stats.protocol_errors << " handler_errors="
            << stats.handler_errors << '\n';
  return 0;
}
