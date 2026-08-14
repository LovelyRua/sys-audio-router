#include "core/control/control_wire_protocol.h"
#include "core/control/session_file_codec.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_virtual_asio_broker_client.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::byte> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(), [](auto value) {
    return static_cast<std::byte>(value);
  });
  return result;
}

sar::control::SessionDocument make_session() {
  sar::control::SessionDocument session;
  session.preset.sample_rate = 48000;
  session.preset.frames_per_block = 128;
  session.preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  session.preset.matrix.inputs = {
      {"wasapi-capture-l", "WASAPI Capture L"},
      {"wasapi-capture-r", "WASAPI Capture R"},
  };
  session.preset.matrix.outputs = {
      {"wasapi-render-l", "WASAPI Render L"},
      {"wasapi-render-r", "WASAPI Render R"},
  };
  for (std::size_t channel = 0; channel < 4; ++channel) {
    const auto suffix = std::to_string(channel + 1);
    session.preset.matrix.inputs.push_back(
        {"asio-output-" + suffix, "ASIO DAW Out " + suffix});
    session.preset.matrix.outputs.push_back(
        {"asio-input-" + suffix, "ASIO DAW In " + suffix});
  }

  session.virtual_asio_devices = {
      {
          .device_id = "studio-a",
          .clsid = "{1D4C6A70-1857-4D79-B04F-23A9DD289A01}",
          .registry_name = "System Audio Route Studio A",
          .broker_token = "studio-a",
          .input_channels = 1,
          .output_channels = 1,
          .enabled = true,
      },
      {
          .device_id = "studio-b",
          .clsid = "{1D4C6A70-1857-4D79-B04F-23A9DD289A02}",
          .registry_name = "System Audio Route Studio B",
          .broker_token = "studio-b",
          .input_channels = 3,
          .output_channels = 3,
          .enabled = true,
      },
  };
  session.audio_runtime.mode = sar::control::AudioRuntimeMode::None;
  session.auto_start = false;
  return session;
}

bool write_session_file(const std::wstring& path) {
  const auto encoded = sar::control::encode_session_file(make_session());
  if (!encoded.ok()) {
    return false;
  }
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                  nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const bool ok = WriteFile(file, encoded.bytes().data(),
                            static_cast<DWORD>(encoded.bytes().size()),
                            &written, nullptr) != FALSE &&
                  written == encoded.bytes().size();
  CloseHandle(file);
  return ok;
}

bool wait_for_pipe(const std::wstring& name) {
  const auto path = L"\\\\.\\pipe\\" + name;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (WaitNamedPipeW(path.c_str(), 100)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

sar::service::NamedPipeControlConfig pipe_config(std::wstring name) {
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = std::move(name);
  return config;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const std::wstring token = std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64());
  const std::wstring control_pipe = L"sar-engine-multi-asio-" + token;
  const std::wstring broker_a_pipe = control_pipe + L"-studio-a";
  const std::wstring broker_b_pipe = control_pipe + L"-studio-b";

  wchar_t temporary_directory[MAX_PATH] = {};
  assert(GetTempPathW(MAX_PATH, temporary_directory) > 0);
  const std::wstring session_path =
      std::wstring(temporary_directory) + L"sar-multi-asio-" + token + L".sar";
  assert(write_session_file(session_path));

  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --pipe " +
                         control_pipe + L" --session \"" + session_path +
                         L"\" --once";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');

  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  assert(job != nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  assert(SetInformationJobObject(job, JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits)));

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  assert(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  assert(AssignProcessToJobObject(job, process.hProcess));
  CloseHandle(process.hThread);

  assert(wait_for_pipe(control_pipe));
  assert(wait_for_pipe(broker_a_pipe));
  assert(wait_for_pipe(broker_b_pipe));

  const auto config_a = pipe_config(broker_a_pipe);
  const auto config_b = pipe_config(broker_b_pipe);
  const auto format_a =
      sar::service::WindowsVirtualAsioBrokerClient::query_format(config_a, 1001);
  const auto format_b =
      sar::service::WindowsVirtualAsioBrokerClient::query_format(config_b, 1001);
  assert(format_a.ok());
  assert(format_b.ok());
  assert(format_a.format.sample_rate == 48000);
  assert(format_a.format.frames_per_block == 128);
  assert(format_a.format.input_channels == 1);
  assert(format_a.format.output_channels == 1);
  assert(format_b.format.sample_rate == 48000);
  assert(format_b.format.frames_per_block == 128);
  assert(format_b.format.input_channels == 3);
  assert(format_b.format.output_channels == 3);

  const std::string shared_client_id = "same-client";
  auto connected_a = sar::service::WindowsVirtualAsioBrokerClient::connect(
      config_a,
      {
          .request_id = 2001,
          .client_id = shared_client_id,
          .format = format_a.format,
          .queue_capacity_blocks = 8,
          .client_nonce_low = 0x0102030405060708ULL,
          .client_nonce_high = 0x1112131415161718ULL,
      });
  auto connected_b = sar::service::WindowsVirtualAsioBrokerClient::connect(
      config_b,
      {
          .request_id = 2001,
          .client_id = shared_client_id,
          .format = format_b.format,
          .queue_capacity_blocks = 8,
          .client_nonce_low = 0x0102030405060708ULL,
          .client_nonce_high = 0x1112131415161718ULL,
      });
  assert(connected_a.ok());
  assert(connected_b.ok());
  auto client_a = connected_a.take_client();
  auto client_b = connected_b.take_client();
  assert(client_a->connected());
  assert(client_b->connected());
  assert(client_a->names() != client_b->names());
  assert(client_a->names().mapping != client_b->names().mapping);
  assert(client_a->names().input_event != client_b->names().input_event);
  assert(client_a->names().output_event != client_b->names().output_event);
  assert(client_a->names().shutdown_event != client_b->names().shutdown_event);
  assert(client_a->header().input_channels == 1);
  assert(client_a->header().output_channels == 1);
  assert(client_b->header().input_channels == 3);
  assert(client_b->header().output_channels == 3);

  assert(client_a->disconnect().ok());
  assert(!client_a->connected());
  assert(client_b->connected());
  assert(client_b->disconnect().ok());

  sar::control::ControlCommand query;
  query.command_id = "stop-once";
  query.type = sar::control::ControlCommandType::QuerySessionState;
  const auto encoded_query = sar::control::encode_control_command(query);
  assert(encoded_query.ok());
  const auto response = sar::service::transact_named_pipe_control(
      pipe_config(control_pipe), as_bytes(encoded_query.bytes));
  assert(response.ok());
  const auto& response_payload = response.payload();
  const auto decoded_response = sar::control::decode_control_response(
      std::vector<std::uint8_t>(
          reinterpret_cast<const std::uint8_t*>(response_payload.data()),
          reinterpret_cast<const std::uint8_t*>(response_payload.data()) +
              response_payload.size()));
  assert(decoded_response.ok());
  assert(decoded_response.response.status ==
         sar::control::ControlResponseStatus::Accepted);

  assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  assert(GetExitCodeProcess(process.hProcess, &exit_code));
  assert(exit_code == 0);
  CloseHandle(process.hProcess);
  CloseHandle(job);
  DeleteFileW((session_path + L".lock").c_str());
  DeleteFileW(session_path.c_str());
}
