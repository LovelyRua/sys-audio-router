#include "core/control/session_file_codec.h"
#include "core/service/engine_control_service.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_virtual_asio_broker_server.h"
#include "core/service/windows_virtual_asio_transport_host.h"
#include "core/service/windows_wasapi_engine_runtime.h"
#include "core/service/windows_wasapi_matrix_runtime.h"
#include "core/service/virtual_asio_matrix_profile.h"
#include "core/service/virtual_asio_instance_layout.h"
#include "core/platform/realtime_audio_channel_slice_sink.h"
#include "core/platform/realtime_audio_fanout_sink.h"
#include "core/platform/realtime_audio_input_assembler.h"
#include "core/platform/virtual_asio_capture_bus.h"
#include "core/platform/realtime_audio_rate_matching_source.h"
#include "core/platform/windows_wasapi_device_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <sddl.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
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

sar::control::PresetDocument initial_preset(std::size_t asio_channels = 2) {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"wasapi-capture-l", "WASAPI Capture L"});
  preset.matrix.inputs.push_back({"wasapi-capture-r", "WASAPI Capture R"});
  preset.matrix.outputs.push_back({"wasapi-render-l", "WASAPI Render L"});
  preset.matrix.outputs.push_back({"wasapi-render-r", "WASAPI Render R"});
  for (std::size_t channel = 0; channel < asio_channels; ++channel) {
    const auto suffix = asio_channels == 2
                            ? (channel == 0 ? std::string{"l"}
                                            : std::string{"r"})
                            : std::to_string(channel + 1);
    preset.matrix.inputs.push_back(
        {"asio-output-" + suffix,
         "ASIO DAW Out " + std::to_string(channel + 1)});
    preset.matrix.outputs.push_back(
        {"asio-input-" + suffix,
         "ASIO DAW In " + std::to_string(channel + 1)});
  }
  const auto routed_channels = std::min<std::size_t>(2, asio_channels);
  for (std::size_t channel = 0; channel < routed_channels; ++channel) {
    const auto suffix = asio_channels == 2
                            ? (channel == 0 ? std::string{"l"}
                                            : std::string{"r"})
                            : std::to_string(channel + 1);
    const auto physical_suffix = channel == 0 ? "l" : "r";
    preset.matrix.routes.push_back(
        {"asio-output-" + suffix,
         std::string{"wasapi-render-"} + physical_suffix, 1.0F, false});
    preset.matrix.routes.push_back(
        {std::string{"wasapi-capture-"} + physical_suffix,
         "asio-input-" + suffix, 1.0F, false});
  }
  return preset;
}

sar::platform::WasapiGraphChannelLayout unified_channel_layout(
    std::size_t graph_input_channels,
    std::size_t graph_output_channels,
    const sar::service::VirtualAsioMatrixProfile& asio) noexcept {
  return {
      .graph_input_channels = graph_input_channels,
      .graph_output_channels = graph_output_channels,
      .capture_input_offset = 0,
      .external_input_offset = asio.daw_output_offset,
      .external_input_channels = asio.channels,
      .render_output_offset = 0,
      .external_output_offset = asio.daw_input_offset,
      .external_output_channels = asio.channels,
  };
}

bool upgrade_legacy_stereo_preset(sar::control::PresetDocument& preset) {
  if (preset.matrix.inputs.size() != 2 ||
      preset.matrix.outputs.size() != 2) {
    return false;
  }

  auto upgraded = initial_preset();
  upgraded.sample_rate = preset.sample_rate;
  upgraded.frames_per_block = preset.frames_per_block;
  upgraded.matrix.routes.clear();
  upgraded.matrix.routes.push_back(
      {"wasapi-capture-l", "asio-input-l", 1.0F, false});
  upgraded.matrix.routes.push_back(
      {"wasapi-capture-r", "asio-input-r", 1.0F, false});
  for (const auto& route : preset.matrix.routes) {
    std::size_t input_index = preset.matrix.inputs.size();
    std::size_t output_index = preset.matrix.outputs.size();
    for (std::size_t index = 0; index < preset.matrix.inputs.size(); ++index) {
      if (preset.matrix.inputs[index].id == route.input_id) {
        input_index = index;
        break;
      }
    }
    for (std::size_t index = 0; index < preset.matrix.outputs.size(); ++index) {
      if (preset.matrix.outputs[index].id == route.output_id) {
        output_index = index;
        break;
      }
    }
    if (input_index >= 2 || output_index >= 2) {
      continue;
    }
    upgraded.matrix.routes.push_back({
        input_index == 0 ? "asio-output-l" : "asio-output-r",
        output_index == 0 ? "wasapi-render-l" : "wasapi-render-r",
        route.gain,
        route.muted,
    });
  }
  preset = std::move(upgraded);
  return true;
}

std::unique_ptr<sar::graph::Graph> make_asio_transport_graph(
    const sar::platform::VirtualAsioFormat& format,
    std::uint64_t version) {
  if (format.input_channels == 0 ||
      format.input_channels != format.output_channels) {
    return {};
  }
  auto graph = std::make_unique<sar::graph::Graph>(
      version, format.input_channels, format.frames_per_block,
      format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::PassthroughNode>());
  return graph;
}

sar::control::SessionDocument default_session(std::size_t asio_channels = 2) {
  sar::control::SessionDocument session;
  session.preset = initial_preset(asio_channels);
  auto asio = sar::control::default_virtual_asio_device_definition();
  asio.input_channels = static_cast<std::uint32_t>(asio_channels);
  asio.output_channels = static_cast<std::uint32_t>(asio_channels);
  session.virtual_asio_devices.push_back(std::move(asio));
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
    sar::platform::RealtimeAudioSource* external_render_input,
    sar::platform::RealtimeAudioSink* external_capture_output,
    sar::platform::WasapiGraphChannelLayout channel_layout) {
  return [external_render_input, external_capture_output, channel_layout](
             const sar::control::AudioRuntimeConfiguration& configuration,
             std::shared_ptr<sar::graph::Graph> graph,
             const sar::control::PresetRouteMatrix& matrix) {
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiRender) {
      if (!configuration.render_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_render(
                configuration.render_device_id, std::move(graph),
                external_render_input, channel_layout));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_render(
              std::move(graph), external_render_input,
              channel_layout));
    }
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiDuplex) {
      if (!configuration.capture_device_id.empty()) {
        return convert_runtime_result(
            sar::service::WindowsWasapiEngineRuntime::open_duplex(
                configuration.capture_device_id,
                configuration.render_device_id,
                std::move(graph),
                external_render_input,
                external_capture_output,
                channel_layout));
      }
      return convert_runtime_result(
          sar::service::WindowsWasapiEngineRuntime::open_default_duplex(
              std::move(graph), external_render_input,
              external_capture_output, channel_layout));
    }
    if (configuration.mode ==
        sar::control::AudioRuntimeMode::WasapiMatrix) {
      const auto asio = sar::service::virtual_asio_matrix_profile(matrix);
      if (!asio.has_value()) {
        return sar::service::EngineAudioRuntimeBuildResult::failure({{
            "virtual_asio_matrix_ports_missing",
            "WASAPI matrix requires equal, contiguous Virtual ASIO input and "
            "output port groups.",
        }});
      }
      auto matrix_layout = channel_layout;
      matrix_layout.graph_input_channels = matrix.inputs.size();
      matrix_layout.graph_output_channels = matrix.outputs.size();
      matrix_layout.external_input_offset = asio->daw_output_offset;
      matrix_layout.external_input_channels = asio->channels;
      matrix_layout.external_output_offset = asio->daw_input_offset;
      matrix_layout.external_output_channels = asio->channels;
      return sar::service::open_windows_wasapi_matrix_runtime(
          configuration, matrix, std::move(graph), external_render_input,
          external_capture_output, matrix_layout);
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
  std::size_t requested_asio_channels = 2;
  bool asio_channels_explicit = false;
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
    } else if (argument == "--asio-channels" && index + 1 < argc) {
      const std::string value = argv[++index];
      std::size_t parsed = 0;
      const auto converted = std::from_chars(
          value.data(), value.data() + value.size(), parsed);
      if (converted.ec != std::errc{} ||
          converted.ptr != value.data() + value.size() || parsed == 0 ||
          parsed > sar::platform::kVirtualAsioMaxChannels) {
        std::cerr << "--asio-channels must be between 1 and "
                  << sar::platform::kVirtualAsioMaxChannels << ".\n";
        return 2;
      }
      requested_asio_channels = parsed;
      asio_channels_explicit = true;
    } else {
      std::cerr << "Usage: sar_engine_service [--pipe NAME] [--once] "
                   "[--session FILE] [--asio-channels COUNT] "
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

  auto desired_session = default_session(requested_asio_channels);
  bool session_writes_allowed = has_session_path;
  bool session_file_missing = false;
  bool session_profile_resized = false;
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

  if (upgrade_legacy_stereo_preset(desired_session.preset)) {
    std::cerr << "session_warning code=legacy_matrix_upgraded "
                 "detail=2x2_to_unified_4x4\n";
  }

  const auto loaded_asio_profile = sar::service::virtual_asio_matrix_profile(
      desired_session.preset.matrix);
  if (asio_channels_explicit && loaded_asio_profile.has_value() &&
      loaded_asio_profile->channels != requested_asio_channels) {
    if (!sar::service::resize_virtual_asio_matrix_profile(
            desired_session.preset, requested_asio_channels)) {
      std::cerr << "virtual_asio_resize_failed: Could not resize the session "
                   "Virtual ASIO port groups.\n";
      return 1;
    }
    std::cerr << "session_warning code=virtual_asio_channels_resized detail="
              << loaded_asio_profile->channels << "_to_"
              << requested_asio_channels << " action=restart_profile\n";
    if (desired_session.virtual_asio_devices.empty()) {
      desired_session.virtual_asio_devices.push_back(
          sar::control::default_virtual_asio_device_definition());
    }
    desired_session.virtual_asio_devices.front().input_channels =
        static_cast<std::uint32_t>(requested_asio_channels);
    desired_session.virtual_asio_devices.front().output_channels =
        static_cast<std::uint32_t>(requested_asio_channels);
    session_profile_resized = true;
  }

  const auto asio_profile = sar::service::virtual_asio_matrix_profile(
      desired_session.preset.matrix);
  if (!asio_profile.has_value()) {
    std::cerr << "unified_matrix_topology_unsupported: The Alpha engine "
                 "requires equal, contiguous Virtual ASIO input and output "
                 "port groups.\n";
    return 1;
  }

  const auto asio_layout = sar::service::virtual_asio_instance_layout(
      desired_session.virtual_asio_devices, *asio_profile);
  if (!asio_layout.has_value()) {
    std::cerr << "virtual_asio_instance_layout_invalid: Enabled Virtual ASIO "
                 "device channel totals must match the matrix port groups.\n";
    return 1;
  }

  std::vector<std::unique_ptr<sar::platform::VirtualAsioRenderBus>>
      asio_render_buses;
  std::vector<std::unique_ptr<sar::platform::VirtualAsioCaptureBus>>
      asio_capture_buses;
  std::vector<std::unique_ptr<sar::platform::RealtimeAudioRateMatchingSource>>
      asio_rate_matchers;
  std::vector<sar::platform::RealtimeAudioInputBinding> asio_input_bindings;
  asio_render_buses.reserve(asio_layout->instances.size());
  asio_capture_buses.reserve(asio_layout->instances.size());
  asio_rate_matchers.reserve(asio_layout->instances.size());
  asio_input_bindings.reserve(asio_layout->instances.size());
  for (const auto& instance : asio_layout->instances) {
    auto render_bus = std::make_unique<sar::platform::VirtualAsioRenderBus>(
        instance.output_channels, desired_session.preset.frames_per_block, 8,
        32);
    auto capture_bus = std::make_unique<sar::platform::VirtualAsioCaptureBus>(
        instance.input_channels, desired_session.preset.frames_per_block, 8,
        32);
    auto rate_matcher =
        std::make_unique<sar::platform::RealtimeAudioRateMatchingSource>(
            *render_bus, instance.output_channels,
            desired_session.preset.frames_per_block,
            desired_session.preset.sample_rate,
            desired_session.preset.sample_rate, 4);
    asio_input_bindings.push_back({
        .source = rate_matcher.get(),
        .destination_first_channel = instance.daw_output_offset,
        .channel_count = instance.output_channels,
    });
    asio_render_buses.push_back(std::move(render_bus));
    asio_capture_buses.push_back(std::move(capture_bus));
    asio_rate_matchers.push_back(std::move(rate_matcher));
  }
  sar::platform::RealtimeAudioInputAssembler asio_input_assembler(
      asio_layout->output_channels, desired_session.preset.frames_per_block,
      std::move(asio_input_bindings));
  std::vector<std::unique_ptr<sar::platform::RealtimeAudioChannelSliceSink>>
      asio_output_slices;
  std::vector<sar::platform::RealtimeAudioSink*> asio_output_slice_ptrs;
  asio_output_slices.reserve(asio_layout->instances.size());
  asio_output_slice_ptrs.reserve(asio_layout->instances.size());
  for (std::size_t index = 0; index < asio_layout->instances.size(); ++index) {
    const auto& instance = asio_layout->instances[index];
    auto slice =
        std::make_unique<sar::platform::RealtimeAudioChannelSliceSink>(
            instance.daw_input_offset, instance.input_channels,
            desired_session.preset.frames_per_block,
            *asio_capture_buses[index]);
    asio_output_slice_ptrs.push_back(slice.get());
    asio_output_slices.push_back(std::move(slice));
  }
  sar::platform::RealtimeAudioFanoutSink asio_output_fanout(
      std::move(asio_output_slice_ptrs));

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
      make_wasapi_runtime_configurator(
          &asio_input_assembler,
          &asio_output_fanout,
          unified_channel_layout(desired_session.preset.matrix.inputs.size(),
                                 desired_session.preset.matrix.outputs.size(),
                                 *asio_profile)));
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
  if ((session_file_missing || session_profile_resized) &&
      session_writes_allowed) {
    if (!save_session_file_atomic(session_path, desired_session)) {
      std::cerr << "session_write_failed: Could not initialize the requested session file.\n";
      service->stop_audio_runtime();
      return 1;
    }
  }

  std::vector<std::unique_ptr<sar::service::WindowsVirtualAsioTransportHost>>
      asio_hosts;
  std::vector<std::unique_ptr<sar::service::WindowsVirtualAsioBrokerServer>>
      asio_brokers;
  asio_hosts.reserve(asio_layout->instances.size());
  asio_brokers.reserve(asio_layout->instances.size());
  for (std::size_t index = 0; index < asio_layout->instances.size(); ++index) {
    const auto& instance = asio_layout->instances[index];
    const auto& definition =
        desired_session.virtual_asio_devices[instance.definition_index];
    asio_hosts.push_back(std::make_unique<
                         sar::service::WindowsVirtualAsioTransportHost>(
        sar::service::WindowsVirtualAsioHostConfig{
            .endpoint_token = definition.device_id,
            .maximum_clients = 8,
            .queue_capacity_blocks = 8,
            .wait_timeout_ms = 20,
        },
        asio_render_buses[index].get(), asio_capture_buses[index].get()));
  }
  service->set_preset_commit_observer(
      [&asio_hosts, &asio_render_buses, &asio_capture_buses, &asio_layout](
          const sar::control::PresetDocument& preset,
          std::uint64_t graph_version) {
        const auto profile = sar::service::virtual_asio_matrix_profile(
            preset.matrix);
        if (!profile.has_value() ||
            profile->channels != asio_layout->output_channels ||
            profile->channels != asio_layout->input_channels) {
          return std::vector<sar::control::PresetError>{{
              "unified_matrix_topology_unsupported",
              "Changing Virtual ASIO channel count requires restarting the "
              "engine service.",
          }};
        }
        for (std::size_t index = 0; index < asio_layout->instances.size();
             ++index) {
          const auto& instance = asio_layout->instances[index];
          if (!asio_render_buses[index]->accepts_consumer_format(
                  instance.output_channels, preset.frames_per_block) ||
              asio_capture_buses[index]->producer_frames() !=
                  preset.frames_per_block) {
            return std::vector<sar::control::PresetError>{{
                "virtual_asio_bus_format_requires_restart",
                "Changing frames per block requires restarting the engine service.",
            }};
          }
        }
        std::vector<sar::control::PresetError> errors;
        for (auto& host : asio_hosts) {
          const auto refreshed = host->refresh_graphs(
              [&preset, graph_version](
                  const sar::platform::VirtualAsioFormat& format) {
                if (format.sample_rate != preset.sample_rate) {
                  return std::unique_ptr<sar::graph::Graph>{};
                }
                return make_asio_transport_graph(format, graph_version);
              });
          for (const auto& error : refreshed.errors) {
            errors.push_back({error.code, error.message});
          }
          if (!errors.empty()) {
            break;
          }
        }
        return errors;
      });
  for (std::size_t index = 0; index < asio_layout->instances.size(); ++index) {
    const auto& instance = asio_layout->instances[index];
    const auto& definition =
        desired_session.virtual_asio_devices[instance.definition_index];
    const std::wstring broker_token(definition.broker_token.begin(),
                                    definition.broker_token.end());
    const auto asio_pipe_name = pipe_config.pipe_name + L"-" + broker_token;
    auto& host = *asio_hosts[index];
    auto broker =
        std::make_unique<sar::service::WindowsVirtualAsioBrokerServer>(
            asio_pipe_name, host,
            [&host](const sar::platform::VirtualAsioFormat& format) {
              return make_asio_transport_graph(format,
                                               host.graph_generation());
            },
            [&service, input_channels = instance.input_channels,
             output_channels = instance.output_channels] {
              const auto session = service->session_document();
              return sar::platform::VirtualAsioFormat{
                  .sample_rate = session.preset.sample_rate,
                  .frames_per_block = session.preset.frames_per_block,
                  .input_channels =
                      static_cast<std::uint32_t>(input_channels),
                  .output_channels =
                      static_cast<std::uint32_t>(output_channels),
              };
            });
    const auto asio_started = broker->start();
    if (!asio_started.ok()) {
      std::cerr << asio_started.error().code << ": "
                << asio_started.error().message << '\n';
      for (auto& started_broker : asio_brokers) {
        started_broker->stop();
      }
      for (auto& started_host : asio_hosts) {
        started_host->stop_all();
      }
      service->stop_audio_runtime();
      return 1;
    }
    asio_brokers.push_back(std::move(broker));
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
    for (auto& broker : asio_brokers) broker->stop();
    for (auto& host : asio_hosts) host->stop_all();
    service->stop_audio_runtime();
    std::cerr << started.error().code << ": " << started.error().message << '\n';
    return 1;
  }

  SetConsoleCtrlHandler(console_handler, TRUE);
  std::cout << "engine_service_state=running pipe=" << pipe_display_name
            << " asio_instances=" << asio_brokers.size() << '\n';
  while (!stop_requested.load()) {
    if (once && pipe_server.stats().completed_requests >= 1) {
      break;
    }
    for (auto& host : asio_hosts) {
      static_cast<void>(host->reap_stopped_sessions());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  pipe_server.stop();
  for (auto& broker : asio_brokers) broker->stop();
  for (auto& host : asio_hosts) host->stop_all();
  service->stop_audio_runtime();
  bool session_write_failed = false;
  if (session_writes_allowed && !save_session_file_atomic(session_path, desired_session)) {
    std::cerr << "session_write_failed: Could not persist the final engine session.\n";
    session_write_failed = true;
  }
  const auto stats = pipe_server.stats();
  std::uint64_t asio_requests = 0;
  for (const auto& broker : asio_brokers) {
    asio_requests += broker->stats().completed_requests;
  }
  std::cout << "engine_service_state=stopped requests="
            << stats.completed_requests << " protocol_errors="
            << stats.protocol_errors << " handler_errors="
            << stats.handler_errors << " asio_requests=" << asio_requests
            << '\n';
  return session_write_failed ? 1 : 0;
}
