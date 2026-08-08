#include "core/control/session_file_codec.h"
#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_virtual_asio_broker_server.h"
#include "core/service/windows_virtual_asio_transport_host.h"
#include "core/service/windows_wasapi_engine_runtime.h"
#include "core/platform/windows_wasapi_device_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cwchar>
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

class SessionFileLock final {
 public:
  explicit SessionFileLock(HANDLE handle) noexcept : handle_(handle) {}
  SessionFileLock(const SessionFileLock&) = delete;
  SessionFileLock& operator=(const SessionFileLock&) = delete;
  ~SessionFileLock() {
    if (handle_ != INVALID_HANDLE_VALUE) {
      CloseHandle(handle_);
    }
  }

 private:
  HANDLE handle_ = INVALID_HANDLE_VALUE;
};

class EngineProcessLock final {
 public:
  explicit EngineProcessLock(const std::wstring& name) noexcept {
    handle_ = CreateMutexW(nullptr, TRUE, name.c_str());
    error_ = handle_ == nullptr ? GetLastError() : ERROR_SUCCESS;
    already_running_ = handle_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS;
  }
  EngineProcessLock(const EngineProcessLock&) = delete;
  EngineProcessLock& operator=(const EngineProcessLock&) = delete;
  ~EngineProcessLock() {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
  }

  [[nodiscard]] bool acquired() const noexcept {
    return handle_ != nullptr && !already_running_;
  }
  [[nodiscard]] DWORD error() const noexcept {
    return handle_ == nullptr ? error_ : ERROR_ALREADY_EXISTS;
  }

 private:
  HANDLE handle_ = nullptr;
  DWORD error_ = ERROR_SUCCESS;
  bool already_running_ = false;
};

bool current_user_sid(std::wstring& value) noexcept {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  DWORD required = 0;
  static_cast<void>(GetTokenInformation(token, TokenUser, nullptr, 0, &required));
  if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    CloseHandle(token);
    return false;
  }
  std::vector<std::byte> storage(required);
  const bool read = GetTokenInformation(token, TokenUser, storage.data(), required,
                                        &required) != FALSE;
  CloseHandle(token);
  if (!read) {
    return false;
  }
  const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
  LPWSTR text = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &text) || text == nullptr) {
    return false;
  }
  value.assign(text);
  LocalFree(text);
  return true;
}

std::uint64_t hash_pipe_name(const std::wstring& pipe_name) noexcept {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const auto character : pipe_name) {
    hash ^= static_cast<std::uint16_t>(character);
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool make_engine_lock_name(const std::wstring& pipe_name,
                           std::wstring& name) noexcept {
  std::wstring sid;
  if (!current_user_sid(sid)) {
    return false;
  }
  wchar_t suffix[17] = {};
  if (std::swprintf(suffix, 17, L"%016llx",
                    static_cast<unsigned long long>(hash_pipe_name(pipe_name))) != 16) {
    return false;
  }
  name = L"Global\\SystemAudioRoute.EngineService." + sid + L"." + suffix;
  return true;
}

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
    desired_session.auto_start = service.audio_runtime_running();
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

sar::service::EngineAudioRuntimeConfigurator make_wasapi_runtime_configurator(
    sar::platform::RealtimeAudioSource* external_render_input) {
  return [external_render_input](
             const sar::control::AudioRuntimeConfiguration& configuration,
             std::shared_ptr<sar::graph::Graph> graph) {
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiRender) {
      if (!configuration.render_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_render(
                configuration.render_device_id, std::move(graph),
                external_render_input));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_render(
              std::move(graph), external_render_input));
    }
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiDuplex) {
      if (!configuration.capture_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_duplex(
                configuration.capture_device_id,
                configuration.render_device_id,
                std::move(graph),
                external_render_input));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_duplex(
              std::move(graph), external_render_input));
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

  std::wstring engine_lock_name;
  if (!make_engine_lock_name(pipe_config.pipe_name, engine_lock_name)) {
    std::cerr << "engine_service_lock_name_failed: Could not create the "
                 "per-user engine lock name.\n";
    return 1;
  }
  EngineProcessLock engine_lock(engine_lock_name);
  if (!engine_lock.acquired()) {
    const auto error = engine_lock.error();
    if (error == ERROR_ALREADY_EXISTS) {
      std::cerr << "engine_service_already_running: Another System Audio Route "
                   "engine already owns this control pipe.\n";
    } else {
      std::cerr << "engine_service_lock_failed: Could not acquire the per-user "
                   "engine lock (win32="
                << error << ").\n";
    }
    return 1;
  }

  auto desired_session = default_session();
  bool session_writes_allowed = has_session_path;
  bool session_file_missing = false;
  std::unique_ptr<SessionFileLock> session_lock;
  if (has_session_path) {
    const auto lock_path = session_path + L".lock";
    const HANDLE lock_handle = CreateFileW(lock_path.c_str(),
                                           GENERIC_READ | GENERIC_WRITE,
                                           0,
                                           nullptr,
                                           OPEN_ALWAYS,
                                           FILE_ATTRIBUTE_NORMAL,
                                           nullptr);
    if (lock_handle == INVALID_HANDLE_VALUE) {
      std::cerr << "session_lock_failed: Another engine may already own this session ("
                << GetLastError() << ").\n";
      return 1;
    }
    session_lock = std::make_unique<SessionFileLock>(lock_handle);
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

  if (desired_session.preset.matrix.inputs.size() != 2 ||
      desired_session.preset.matrix.outputs.size() != 2) {
    std::cerr << "virtual_asio_driver_topology_unsupported: The Alpha ASIO "
                 "driver requires a 2-input, 2-output preset.\n";
    return 1;
  }

  const auto render_bus_channels = std::max(
      desired_session.preset.matrix.inputs.size(),
      desired_session.preset.matrix.outputs.size());
  sar::platform::VirtualAsioRenderBus asio_render_bus(
      render_bus_channels, desired_session.preset.frames_per_block, 8, 32);

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
      make_wasapi_runtime_configurator(&asio_render_bus));
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
    if (!save_session_file_atomic(session_path, desired_session)) {
      std::cerr << "session_write_failed: Could not initialize the requested session file.\n";
      service->stop_audio_runtime();
      return 1;
    }
  }

  const std::wstring asio_pipe_name =
      pipe_config.pipe_name + L"-virtual-asio";
  sar::service::WindowsVirtualAsioTransportHost asio_host({
      .endpoint_token = "engine",
      .maximum_clients = 8,
      .queue_capacity_blocks = 8,
      .wait_timeout_ms = 20,
  }, &asio_render_bus);
  service->set_preset_commit_observer(
      [&asio_host, &asio_render_bus](
          const sar::control::PresetDocument& preset,
          std::uint64_t graph_version) {
        if (preset.matrix.inputs.size() != 2 ||
            preset.matrix.outputs.size() != 2) {
          return std::vector<sar::control::PresetError>{{
              "virtual_asio_driver_topology_unsupported",
              "The Alpha Virtual ASIO driver supports exactly two inputs and "
              "two outputs.",
          }};
        }
        if (!asio_render_bus.accepts_consumer_format(
                2, preset.frames_per_block)) {
          return std::vector<sar::control::PresetError>{{
              "virtual_asio_render_bus_format_requires_restart",
              "Changing frames per block requires restarting the engine service.",
          }};
        }
        const auto refreshed = asio_host.refresh_graphs(
            [&preset, graph_version](
                const sar::platform::VirtualAsioFormat& format) {
              if (format.sample_rate != preset.sample_rate ||
                  format.input_channels != format.output_channels ||
                  format.input_channels != preset.matrix.inputs.size() ||
                  format.output_channels != preset.matrix.outputs.size()) {
                return std::unique_ptr<sar::graph::Graph>{};
              }
              auto client_preset = preset;
              client_preset.frames_per_block = format.frames_per_block;
              auto built = sar::control::build_preset_graph(
                  client_preset, graph_version);
              return built.ok() ? built.take_graph()
                                : std::unique_ptr<sar::graph::Graph>{};
            });
        std::vector<sar::control::PresetError> errors;
        errors.reserve(refreshed.errors.size());
        for (const auto& error : refreshed.errors) {
          errors.push_back({error.code, error.message});
        }
        return errors;
      });
  sar::service::WindowsVirtualAsioBrokerServer asio_broker(
      asio_pipe_name, asio_host,
      [&service](const sar::platform::VirtualAsioFormat& format) {
        return service->build_client_graph(
            format.sample_rate, format.frames_per_block,
            format.input_channels, format.output_channels);
      },
      [&service] {
        const auto session = service->session_document();
        return sar::platform::VirtualAsioFormat{
            .sample_rate = session.preset.sample_rate,
            .frames_per_block = session.preset.frames_per_block,
            .input_channels = static_cast<std::uint32_t>(
                session.preset.matrix.inputs.size()),
            .output_channels = static_cast<std::uint32_t>(
                session.preset.matrix.outputs.size()),
        };
      });
  const auto asio_started = asio_broker.start();
  if (!asio_started.ok()) {
    std::cerr << asio_started.error().code << ": "
              << asio_started.error().message << '\n';
    service->stop_audio_runtime();
    return 1;
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
            static_cast<void>(
                save_session_file_atomic(session_path, desired_session));
          }
        }
        return sar::service::NamedPipeControlResult::success(
            as_bytes(response.bytes));
      });
  const auto started = pipe_server.start();
  if (!started.ok()) {
    asio_broker.stop();
    asio_host.stop_all();
    service->stop_audio_runtime();
    std::cerr << started.error().code << ": " << started.error().message << '\n';
    return 1;
  }

  SetConsoleCtrlHandler(console_handler, TRUE);
  std::cout << "engine_service_state=running pipe=" << pipe_display_name
            << " asio_pipe=" << pipe_display_name << "-virtual-asio\n";
  while (!stop_requested.load()) {
    if (once && pipe_server.stats().completed_requests >= 1) {
      break;
    }
    static_cast<void>(asio_host.reap_stopped_sessions());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  pipe_server.stop();
  asio_broker.stop();
  asio_host.stop_all();
  service->stop_audio_runtime();
  bool session_write_failed = false;
  if (session_writes_allowed && !save_session_file_atomic(session_path, desired_session)) {
    std::cerr << "session_write_failed: Could not persist the final engine session.\n";
    session_write_failed = true;
  }
  const auto stats = pipe_server.stats();
  std::cout << "engine_service_state=stopped requests="
            << stats.completed_requests << " protocol_errors="
            << stats.protocol_errors << " handler_errors="
            << stats.handler_errors << " asio_requests="
            << asio_broker.stats().completed_requests << '\n';
  return session_write_failed ? 1 : 0;
}
