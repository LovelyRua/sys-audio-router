#include "core/control/control_wire_protocol.h"
#include "core/realtime/audio_buffer.h"
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
#include <vector>

namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::byte> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(), [](auto value) {
    return static_cast<std::byte>(value);
  });
  return result;
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

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const std::wstring token = std::to_wstring(GetCurrentProcessId()) + L"-" +
                             std::to_wstring(GetTickCount64());
  const std::wstring control_pipe = L"sar-engine-asio-integration-" + token;
  const std::wstring asio_pipe = control_pipe + L"-virtual-asio";
  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --pipe " +
                         control_pipe + L" --once";
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
  assert(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &process));
  assert(AssignProcessToJobObject(job, process.hProcess));
  CloseHandle(process.hThread);
  assert(wait_for_pipe(control_pipe));
  assert(wait_for_pipe(asio_pipe));

  sar::service::NamedPipeControlConfig asio_config;
  asio_config.pipe_name = asio_pipe;
  auto connected = sar::service::WindowsVirtualAsioBrokerClient::connect(
      asio_config,
      {
          .request_id = 9001,
          .client_id = "engine-process-smoke",
          .format = {48000, 128, 2, 2},
          .queue_capacity_blocks = 8,
          .client_nonce_low = 0x0102030405060708ULL,
          .client_nonce_high = 0x1112131415161718ULL,
      });
  assert(connected.ok());
  auto client = connected.take_client();

  sar::realtime::AudioBuffer input(2, 128);
  sar::realtime::AudioBuffer output(2, 128);
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      input.channel(channel)[frame] =
          static_cast<float>(channel * 128 + frame) / 512.0F;
    }
  }
  const sar::platform::VirtualAsioSharedBlockMetadata sent{
      .sequence = 1,
      .sample_position = 128,
      .qpc_position_100ns = 123456,
  };
  assert(client->push_input(input, sent) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client->signal_input());
  assert(client->wait_output_or_shutdown(2000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);
  sar::platform::VirtualAsioSharedBlockMetadata received;
  assert(client->pop_output(output, received) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(received.sequence == sent.sequence);
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      assert(output.channel(channel)[frame] == input.channel(channel)[frame]);
    }
  }
  assert(client->disconnect().ok());

  sar::control::ControlCommand query;
  query.command_id = "stop-once";
  query.type = sar::control::ControlCommandType::QuerySessionState;
  const auto encoded = sar::control::encode_control_command(query);
  assert(encoded.ok());
  sar::service::NamedPipeControlConfig control_config;
  control_config.pipe_name = control_pipe;
  const auto response = sar::service::transact_named_pipe_control(
      control_config, as_bytes(encoded.bytes));
  assert(response.ok());

  assert(WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  assert(GetExitCodeProcess(process.hProcess, &exit_code));
  assert(exit_code == 0);
  CloseHandle(process.hProcess);
  CloseHandle(job);
}
