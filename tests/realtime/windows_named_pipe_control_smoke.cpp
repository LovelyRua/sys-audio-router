#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

}  // namespace

int main() {
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = L"sys-audio-route-control-smoke-" +
                     std::to_wstring(GetCurrentProcessId());
  config.maximum_message_bytes = 256;

  sar::service::WindowsNamedPipeControlServer server(
      config,
      [](std::span<const std::byte> request) {
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

  const auto unavailable =
      sar::service::transact_named_pipe_control(config, bytes("after-stop"), 20);
  assert(!unavailable.ok());
}
