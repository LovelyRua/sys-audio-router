#include "core/graph/graph.h"
#include "core/service/windows_virtual_asio_broker_server.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

std::unique_ptr<sar::graph::Graph> make_graph(
    const sar::platform::VirtualAsioFormat& format) {
  auto graph = std::make_unique<sar::graph::Graph>(
      1, format.input_channels, format.frames_per_block, format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::GainNode>(1.0F));
  return graph;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64());
  const auto pipe_name = L"sar-asio-soak-smoke-" + token;
  sar::service::WindowsVirtualAsioTransportHost host({
      .endpoint_token = "asio-soak-smoke",
      .maximum_clients = 4,
      .queue_capacity_blocks = 8,
      .wait_timeout_ms = 5,
  });
  sar::service::WindowsVirtualAsioBrokerServer server(
      pipe_name, host, make_graph,
      [] { return sar::platform::VirtualAsioFormat{48000, 128, 2, 2}; });
  assert(server.start().ok());

  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --pipe " +
                         pipe_name +
                         L" --duration-ms 500 --clients 3"
                         L" --block-sizes 64,128,256 --wait-timeout-ms 100"
                         L" --minimum-callback-percent 60";
  std::vector<wchar_t> mutable_command(command.begin(), command.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  assert(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  CloseHandle(process.hThread);
  assert(WaitForSingleObject(process.hProcess, 10'000) == WAIT_OBJECT_0);
  DWORD exit_code = 1;
  assert(GetExitCodeProcess(process.hProcess, &exit_code));
  CloseHandle(process.hProcess);
  assert(exit_code == 0);

  for (std::size_t attempt = 0;
       attempt < 200 && host.active_session_count() != 0; ++attempt) {
    static_cast<void>(host.reap_stopped_sessions());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(host.active_session_count() == 0);
  server.stop();
}
