#include "core/control/session_file_codec.h"
#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_wasapi_engine_runtime.h"
#include "core/platform/windows_wasapi_device_provider.h"

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <utility>
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

sar::control::SessionDocument default_session() {
  sar::control::SessionDocument session;
  session.preset = initial_preset();
  session.audio_runtime.mode = sar::control::AudioRuntimeMode::None;
  session.auto_start = false;
  return session;
}

bool utf8_to_wide(const std::string& input, std::wstring& output) {
  if (input.empty() ||
      input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  const auto input_size = static_cast<int>(input.size());
  const int required = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), input_size, nullptr, 0);
  if (required <= 0) {
    return false;
  }
  output.resize(static_cast<std::size_t>(required));
  return MultiByteToWideChar(CP_UTF8,
                             MB_ERR_INVALID_CHARS,
                             input.data(),
                             input_size,
                             output.data(),
                             required) == required;
}

enum class SessionLoadStatus {
  Missing,
  Loaded,
  PreserveOriginal,
};

struct SessionLoadResult {
  SessionLoadStatus status = SessionLoadStatus::Missing;
  sar::control::SessionDocument session;
  std::string error_code;
};

SessionLoadResult load_session_file(const std::wstring& path) {
  const HANDLE file = CreateFileW(path.c_str(),
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      return {SessionLoadStatus::Missing, default_session(), {}};
    }
    return {SessionLoadStatus::PreserveOriginal,
            default_session(),
            "session_file_read_error_" + std::to_string(error)};
  }

  LARGE_INTEGER size{};
  if (!GetFileSizeEx(file, &size)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    return {SessionLoadStatus::PreserveOriginal,
            default_session(),
            "session_file_size_error_" + std::to_string(error)};
  }
  if (size.QuadPart < 0 ||
      static_cast<unsigned long long>(size.QuadPart) >
          sar::control::kSessionFileMaxBytes) {
    CloseHandle(file);
    return {SessionLoadStatus::PreserveOriginal,
            default_session(),
            "session_file_too_large"};
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD read = 0;
    const auto remaining = bytes.size() - offset;
    const DWORD requested = static_cast<DWORD>(remaining);
    if (!ReadFile(file, bytes.data() + offset, requested, &read, nullptr)) {
      const DWORD error = GetLastError();
      CloseHandle(file);
      return {SessionLoadStatus::PreserveOriginal,
              default_session(),
              "session_file_read_error_" + std::to_string(error)};
    }
    if (read == 0) {
      CloseHandle(file);
      return {SessionLoadStatus::PreserveOriginal,
              default_session(),
              "session_file_unexpected_eof"};
    }
    offset += read;
  }
  CloseHandle(file);

  auto decoded = sar::control::decode_session_file(bytes);
  if (!decoded.ok()) {
    return {SessionLoadStatus::PreserveOriginal,
            default_session(),
            decoded.error().code};
  }
  return {SessionLoadStatus::Loaded, decoded.take_session(), {}};
}

void report_session_write_error(const std::string& code) {
  std::cerr << "session_warning code=session_write_failed detail=" << code
            << " command_state=applied\n";
}

bool write_all(HANDLE file, std::span<const std::uint8_t> bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written = 0;
    const auto remaining = bytes.size() - offset;
    const DWORD requested = static_cast<DWORD>(remaining);
    if (!WriteFile(file, bytes.data() + offset, requested, &written, nullptr) ||
        written == 0) {
      return false;
    }
    offset += written;
  }
  return true;
}

bool save_session_file_atomic(const std::wstring& path,
                              const sar::control::SessionDocument& session) {
  auto encoded = sar::control::encode_session_file(session);
  if (!encoded.ok()) {
    report_session_write_error(encoded.error().code);
    return false;
  }

  const std::wstring temporary =
      path + L".tmp." + std::to_wstring(GetCurrentProcessId());
  const HANDLE file = CreateFileW(temporary.c_str(),
                                  GENERIC_WRITE,
                                  0,
                                  nullptr,
                                  CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    report_session_write_error("session_temp_create_error_" +
                               std::to_string(GetLastError()));
    return false;
  }

  const bool wrote = write_all(file, encoded.bytes());
  const DWORD write_error = wrote ? ERROR_SUCCESS : GetLastError();
  const bool flushed = wrote && FlushFileBuffers(file) != FALSE;
  const DWORD flush_error = flushed ? ERROR_SUCCESS : GetLastError();
  CloseHandle(file);
  if (!wrote || !flushed) {
    DeleteFileW(temporary.c_str());
    report_session_write_error(
        std::string{wrote ? "session_temp_flush_error_"
                          : "session_temp_write_error_"} +
        std::to_string(wrote ? flush_error : write_error));
    return false;
  }

  if (!MoveFileExW(temporary.c_str(),
                   path.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const DWORD error = GetLastError();
    DeleteFileW(temporary.c_str());
    report_session_write_error("session_replace_error_" +
                               std::to_string(error));
    return false;
  }
  return true;
}

bool command_changes_persisted_session(
    sar::control::ControlCommandType type) noexcept {
  return sar::control::control_command_mutates_preset(type) ||
         type == sar::control::ControlCommandType::ConfigureAudioRuntime ||
         type == sar::control::ControlCommandType::StartAudioRuntime ||
         type == sar::control::ControlCommandType::StopAudioRuntime;
}

void merge_successful_command(
    const sar::control::ControlCommand& command,
    const sar::service::EngineControlService& service,
    sar::control::SessionDocument& desired_session) {
  if (sar::control::control_command_mutates_preset(command.type)) {
    desired_session.preset = service.session_document().preset;
    return;
  }
  if (command.type ==
      sar::control::ControlCommandType::ConfigureAudioRuntime) {
    desired_session.audio_runtime = command.audio_runtime;
    desired_session.auto_start = false;
  } else if (command.type ==
             sar::control::ControlCommandType::StartAudioRuntime) {
    desired_session.auto_start = true;
  } else if (command.type ==
             sar::control::ControlCommandType::StopAudioRuntime) {
    desired_session.auto_start = false;
  }
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
  std::wstring session_path;
  bool has_session_path = false;
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
    } else if (argument == "--session" && index + 1 < argc) {
      has_session_path = true;
      if (!utf8_to_wide(argv[++index], session_path)) {
        std::cerr << "--session requires a valid UTF-8 Windows path.\n";
        return 2;
      }
    } else {
      std::cerr << "Usage: sar_engine_service [--pipe NAME] [--once] "
                   "[--session FILE] "
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
  if (has_session_path && (wasapi_render || wasapi_duplex || has_capture_id ||
                           has_render_id)) {
    std::cerr << "--session cannot be combined with WASAPI startup options.\n";
    return 2;
  }

  auto desired_session = default_session();
  bool session_writes_allowed = has_session_path;
  bool session_file_missing = false;
  if (has_session_path) {
    auto loaded = load_session_file(session_path);
    desired_session = std::move(loaded.session);
    session_file_missing = loaded.status == SessionLoadStatus::Missing;
    if (loaded.status == SessionLoadStatus::PreserveOriginal) {
      session_writes_allowed = false;
      std::cerr << "session_warning code=session_load_failed detail="
                << loaded.error_code
                << " action=use_default_preserve_original\n";
    }
  }

  auto service_result =
      sar::service::EngineControlService::create(desired_session.preset);
  if (!service_result.ok()) {
    for (const auto& error : service_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }
  auto service = service_result.take_service();
  service->add_audio_device_provider(
      std::make_unique<sar::platform::WindowsWasapiDeviceProvider>());
  service->set_audio_runtime_configurator(
      make_wasapi_runtime_configurator());
  if (has_session_path &&
      desired_session.audio_runtime.mode !=
          sar::control::AudioRuntimeMode::None) {
    const auto configure_result =
        service->configure_audio_runtime(desired_session.audio_runtime);
    if (!configure_result.ok()) {
      for (const auto& error : configure_result.errors()) {
        std::cerr << "session_warning code=runtime_restore_failed detail="
                  << error.code << " action=keep_service_online\n";
      }
    } else if (desired_session.auto_start) {
      const auto start_result = service->start_audio_runtime();
      if (!start_result.ok()) {
        for (const auto& error : start_result.errors()) {
          std::cerr << "session_warning code=runtime_start_restore_failed detail="
                    << error.code << " action=keep_service_online\n";
        }
      }
    }
  } else if (wasapi_render || wasapi_duplex) {
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
  if (session_file_missing && session_writes_allowed) {
    save_session_file_atomic(session_path, desired_session);
  }

  std::mutex request_mutex;
  sar::service::WindowsNamedPipeControlServer pipe_server(
      pipe_config,
      [&service,
       &request_mutex,
       &desired_session,
       &session_path,
       session_writes_allowed](std::span<const std::byte> request) {
        std::lock_guard request_lock(request_mutex);
        const auto request_bytes = as_u8(request);
        const auto command = sar::control::decode_control_command(request_bytes);
        const auto response = service->handle_wire_request(request_bytes);
        if (!response.ok()) {
          return sar::service::NamedPipeControlResult::failure(
              {"control_response_encode_failed",
               "Engine control response could not be encoded.", 0});
        }
        if (session_writes_allowed && command.ok() &&
            command_changes_persisted_session(command.command.type)) {
          const auto decoded_response =
              sar::control::decode_control_response(response.bytes);
          if (decoded_response.ok() &&
              decoded_response.response.status ==
                  sar::control::ControlResponseStatus::Accepted) {
            merge_successful_command(
                command.command, *service, desired_session);
            save_session_file_atomic(session_path, desired_session);
          }
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
  service->stop_audio_runtime();
  if (session_writes_allowed) {
    save_session_file_atomic(session_path, desired_session);
  }
  const auto stats = pipe_server.stats();
  std::cout << "engine_service_state=stopped requests="
            << stats.completed_requests << " protocol_errors="
            << stats.protocol_errors << " handler_errors="
            << stats.handler_errors << '\n';
  return 0;
}
