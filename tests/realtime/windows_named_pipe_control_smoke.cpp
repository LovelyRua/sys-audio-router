#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

std::vector<std::byte> bytes(std::string_view text) {
  std::vector<std::byte> result(text.size());
  std::transform(text.begin(), text.end(), result.begin(), [](char value) {
    return static_cast<std::byte>(static_cast<unsigned char>(value));
  });
  return result;
}

bool equals_text(std::span<const std::byte> value, std::string_view text) {
  return value.size() == text.size() &&
         std::equal(value.begin(), value.end(), text.begin(), [](std::byte byte, char character) {
           return byte == static_cast<std::byte>(static_cast<unsigned char>(character));
         });
}

}  // namespace

int main() {
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = L"sys-audio-route-control-smoke-" +
                     std::to_wstring(GetCurrentProcessId());
  config.maximum_message_bytes = 256;
  config.request_timeout_ms = 150;

  std::atomic<std::uint32_t> observed_process_id = 0;
  sar::service::WindowsNamedPipeControlServer server(
      config,
      [&observed_process_id](const sar::service::NamedPipeControlPeer& peer,
                             std::span<const std::byte> request) {
        observed_process_id.store(peer.process_id);
        if (equals_text(request, "slow-response")) {
          std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        std::vector<std::byte> response(request.rbegin(), request.rend());
        return sar::service::NamedPipeControlResult::success(std::move(response));
      });

  const auto start = server.start();
  assert(start.ok());
  assert(server.running());

  for (std::uint32_t index = 0; index < 32; ++index) {
    auto request = bytes("request-" + std::to_string(index));
    auto expected = request;
    std::reverse(expected.begin(), expected.end());
    auto response = sar::service::transact_named_pipe_control(config, request);
    if (!response.ok()) {
      std::cerr << "Named-pipe request failed: " << response.error().code
                << " native=" << response.error().native_error << '\n';
    }
    assert(response.ok());
    assert(response.payload() == expected);
  }

  std::vector<std::byte> oversized(config.maximum_message_bytes + 1);
  const auto rejected =
      sar::service::transact_named_pipe_control(config, oversized);
  assert(!rejected.ok());
  assert(rejected.error().code == "pipe_request_too_large");

  const auto before_stop = std::chrono::steady_clock::now();
  server.stop();
  const auto stop_elapsed = std::chrono::steady_clock::now() - before_stop;
  assert(stop_elapsed < std::chrono::seconds(2));
  assert(!server.running());

  const auto stats = server.stats();
  assert(stats.completed_requests == 32);
  assert(stats.accepted_connections >= 32);
  assert(stats.protocol_errors == 0);
  assert(stats.handler_errors == 0);
  assert(observed_process_id.load() == GetCurrentProcessId());

  const auto unavailable =
      sar::service::transact_named_pipe_control(config, bytes("after-stop"), 20);
  assert(!unavailable.ok());

  for (std::uint32_t iteration = 0; iteration < 128; ++iteration) {
    const auto restart = server.start();
    assert(restart.ok());
    const auto restart_stop_begin = std::chrono::steady_clock::now();
    server.stop();
    assert(std::chrono::steady_clock::now() - restart_stop_begin <
           std::chrono::seconds(2));
  }

  const auto stalled_start = server.start();
  assert(stalled_start.ok());
  const auto full_pipe_name = L"\\\\.\\pipe\\" + config.pipe_name;
  HANDLE stalled_client =
      CreateFileW(full_pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  assert(stalled_client != INVALID_HANDLE_VALUE);
  const auto concurrent_response = sar::service::transact_named_pipe_control(
      config, bytes("concurrent-request"), 1000);
  assert(concurrent_response.ok());

  const auto timed_out_response = sar::service::transact_named_pipe_control(
      config, bytes("slow-response"), 25);
  assert(!timed_out_response.ok());
  assert(timed_out_response.error().code == "pipe_read_timeout");

  const auto stalled_stop_begin = std::chrono::steady_clock::now();
  server.stop();
  assert(std::chrono::steady_clock::now() - stalled_stop_begin <
         std::chrono::seconds(2));
  CloseHandle(stalled_client);
}
