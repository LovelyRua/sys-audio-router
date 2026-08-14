#include "core/control/control_wire_protocol.h"
#include "core/control/session_file_codec.h"
#include "core/service/windows_named_pipe_control.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::byte> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(), [](auto value) {
    return static_cast<std::byte>(value);
  });
  return result;
}

std::vector<std::uint8_t> as_u8(const std::vector<std::byte>& bytes) {
  std::vector<std::uint8_t> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(), [](auto value) {
    return std::to_integer<std::uint8_t>(value);
  });
  return result;
}

sar::control::SessionDocument initial_session() {
  sar::control::SessionDocument session;
  session.preset.sample_rate = 48000;
  session.preset.frames_per_block = 128;
  session.preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  session.preset.matrix.inputs = {
      {"wasapi-capture-l", "WASAPI Capture L"},
      {"wasapi-capture-r", "WASAPI Capture R"},
      {"asio-output-l", "ASIO DAW Out 1"},
      {"asio-output-r", "ASIO DAW Out 2"},
  };
  session.preset.matrix.outputs = {
      {"wasapi-render-l", "WASAPI Render L"},
      {"wasapi-render-r", "WASAPI Render R"},
      {"asio-input-l", "ASIO DAW In 1"},
      {"asio-input-r", "ASIO DAW In 2"},
  };
  session.virtual_asio_devices.push_back(
      sar::control::default_virtual_asio_device_definition());
  return session;
}

void write_file(const std::wstring& path, const std::vector<std::uint8_t>& data) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  assert(file != INVALID_HANDLE_VALUE);
  DWORD written = 0;
  assert(WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                   nullptr));
  assert(written == data.size());
  CloseHandle(file);
}

std::vector<std::uint8_t> read_file(const std::wstring& path) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  assert(file != INVALID_HANDLE_VALUE);
  LARGE_INTEGER size{};
  assert(GetFileSizeEx(file, &size));
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size.QuadPart));
  DWORD read = 0;
  assert(ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read,
                  nullptr));
  assert(read == data.size());
  CloseHandle(file);
  return data;
}

bool wait_for_pipe(const std::wstring& name) {
  const auto path = L"\\\\.\\pipe\\" + name;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (WaitNamedPipeW(path.c_str(), 100)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

bool registry_key_exists(const std::wstring& clsid) {
  HKEY key = nullptr;
  const auto path = L"Software\\Classes\\CLSID\\" + clsid;
  const auto status = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                                    KEY_QUERY_VALUE, &key);
  if (key != nullptr) RegCloseKey(key);
  return status == ERROR_SUCCESS;
}

void delete_registration(const std::wstring& clsid,
                         const std::wstring& registry_name) {
  const auto clsid_path = L"Software\\Classes\\CLSID\\" + clsid;
  const auto asio_path = L"Software\\ASIO\\" + registry_name;
  static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, clsid_path.c_str()));
  static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, asio_path.c_str()));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const std::wstring clsid_a = L"{705B4C39-BEB4-47E7-8FA9-B61F1C901A01}";
  const std::wstring clsid_b = L"{705B4C39-BEB4-47E7-8FA9-B61F1C901A02}";
  const std::wstring name_a = L"SAR Topology Test A";
  const std::wstring name_b = L"SAR Topology Test B";
  delete_registration(clsid_a, name_a);
  delete_registration(clsid_b, name_b);

  const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64());
  const auto directory =
      std::filesystem::temp_directory_path() / (L"sar-topology-" + token);
  std::filesystem::create_directories(directory);
  const auto session_path = (directory / L"session.sarsession").wstring();
  const auto encoded_session =
      sar::control::encode_session_file(initial_session());
  assert(encoded_session.ok());
  write_file(session_path, encoded_session.bytes());

  const std::wstring control_pipe = L"sar-topology-control-" + token;
  std::wstring command_line = L"\"" + std::wstring(argv[1]) +
                              L"\" --pipe " + control_pipe + L" --session \"" +
                              session_path + L"\"";
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION process{};
  assert(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  CloseHandle(process.hThread);
  assert(wait_for_pipe(control_pipe));

  sar::control::ControlCommand configure;
  configure.command_id = "configure-two-asio-devices";
  configure.type =
      sar::control::ControlCommandType::ConfigureVirtualAsioDevices;
  configure.virtual_asio_devices = {
      {.device_id = "topology-a",
       .clsid = "{705B4C39-BEB4-47E7-8FA9-B61F1C901A01}",
       .registry_name = "SAR Topology Test A",
       .broker_token = "topology-a",
       .input_channels = 2,
       .output_channels = 2},
      {.device_id = "topology-b",
       .clsid = "{705B4C39-BEB4-47E7-8FA9-B61F1C901A02}",
       .registry_name = "SAR Topology Test B",
       .broker_token = "topology-b",
       .input_channels = 2,
       .output_channels = 2},
  };
  const auto encoded = sar::control::encode_control_command(configure);
  assert(encoded.ok());
  sar::service::NamedPipeControlConfig pipe_config;
  pipe_config.pipe_name = control_pipe;
  const auto transaction = sar::service::transact_named_pipe_control(
      pipe_config, as_bytes(encoded.bytes), 5000);
  assert(transaction.ok());
  const auto response =
      sar::control::decode_control_response(as_u8(transaction.payload()));
  assert(response.ok());
  assert(response.response.status ==
         sar::control::ControlResponseStatus::Accepted);
  assert(response.response.has_virtual_asio_devices);
  assert(response.response.virtual_asio_devices.size() == 2);

  assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  assert(GetExitCodeProcess(process.hProcess, &exit_code));
  assert(exit_code == 0);
  CloseHandle(process.hProcess);

  const auto decoded_session =
      sar::control::decode_session_file(read_file(session_path));
  assert(decoded_session.ok());
  assert(decoded_session.session().virtual_asio_devices.size() == 2);
  assert(decoded_session.session().preset.matrix.inputs.size() == 6);
  assert(decoded_session.session().preset.matrix.outputs.size() == 6);
  assert(registry_key_exists(clsid_a));
  assert(registry_key_exists(clsid_b));

  delete_registration(clsid_a, name_a);
  delete_registration(clsid_b, name_b);
  std::filesystem::remove_all(directory);
}
