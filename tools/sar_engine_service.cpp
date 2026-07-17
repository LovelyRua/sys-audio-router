#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_wasapi_engine_runtime.h"

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

sar::service::EngineAudioRuntimeBuildResult convert_runtime_result(
    sar::service::WindowsWasapiEngineRuntimeOpenResult result) {
  if (!result.ok()) {
    return sar::service::EngineAudioRuntimeBuildResult::failure(result.errors());
  }
  return sar::service::EngineAudioRuntimeBuildResult::success(
      result.take_runtime());
}

sar::service::EngineAudioRuntimeConfigurator make_wasapi_runtime_configurator() {
  return [](const sar::control::AudioRuntimeConfiguration& configuration,
            std::shared_ptr<sar::graph::Graph> graph) {
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiRender) {
      if (!configuration.render_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_render(
                configuration.render_device_id, std::move(graph)));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_render(
              std::move(graph)));
    }
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiDuplex) {
      if (!configuration.capture_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_duplex(
                configuration.capture_device_id,
                configuration.render_device_id,
                std::move(graph)));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_duplex(
              std::move(graph)));
    }
    return sar::service::EngineAudioRuntimeBuildResult::failure({
        {"unsupported_audio_runtime_mode",
         "Windows service does not support the requested runtime mode."},
    });
  };
}

}  // namespace

int main(int argc, char** argv) {
  sar::service::NamedPipeControlConfig pipe_config;
  std::string pipe_display_name = "sys-audio-route-control";
  bool once = false;
  bool wasapi_render = false;
  bool wasapi_duplex = false;
  std::string capture_device_id;
  std::string render_device_id;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--once") {
      once = true;
    } else if (argument == "--wasapi-render") {
      wasapi_render = true;
    } else if (argument == "--wasapi-duplex") {
      wasapi_duplex = true;
    } else if (argument == "--capture-id" && index + 1 < argc) {
      capture_device_id = argv[++index];
    } else if (argument == "--render-id" && index + 1 < argc) {
      render_device_id = argv[++index];
    } else if (argument == "--pipe" && index + 1 < argc) {
      const std::string name = argv[++index];
      pipe_config.pipe_name.assign(name.begin(), name.end());
      pipe_display_name = name;
    } else {
      std::cerr << "Usage: sar_engine_service [--pipe NAME] [--once] "
                   "[--wasapi-render|--wasapi-duplex "
                   "[--capture-id ID --render-id ID]]\n";
      return 2;
    }
  }
  if (wasapi_render && wasapi_duplex) {
    std::cerr << "Choose either --wasapi-render or --wasapi-duplex.\n";
    return 2;
  }
  const bool has_capture_id = !capture_device_id.empty();
  const bool has_render_id = !render_device_id.empty();
  if ((wasapi_duplex && has_capture_id != has_render_id) ||
      (wasapi_render && has_capture_id) ||
      (!wasapi_render && !wasapi_duplex &&
       (has_capture_id || has_render_id))) {
    std::cerr << "Render mode accepts an optional render ID; duplex mode "
                 "requires both endpoint IDs or neither.\n";
    return 2;
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
  service->set_audio_runtime_configurator(
      make_wasapi_runtime_configurator());
  if (wasapi_render || wasapi_duplex) {
    sar::control::AudioRuntimeConfiguration configuration;
    configuration.mode = wasapi_duplex
                             ? sar::control::AudioRuntimeMode::WasapiDuplex
                             : sar::control::AudioRuntimeMode::WasapiRender;
    configuration.capture_device_id = capture_device_id;
    configuration.render_device_id = render_device_id;
    auto configure_result =
        service->configure_audio_runtime(std::move(configuration));
    if (!configure_result.ok()) {
      for (const auto& error : configure_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    auto start_result = service->start_audio_runtime();
    if (!start_result.ok()) {
      for (const auto& error : start_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
  }
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
  service->stop_audio_runtime();
  pipe_server.stop();
  const auto stats = pipe_server.stats();
  std::cout << "engine_service_state=stopped requests="
            << stats.completed_requests << " protocol_errors="
            << stats.protocol_errors << " handler_errors="
            << stats.handler_errors << '\n';
  return 0;
}
