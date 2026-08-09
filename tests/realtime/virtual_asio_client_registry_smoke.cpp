#include "core/platform/virtual_asio_client_registry.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

sar::platform::VirtualAsioClientRequest request(
    std::string id,
    std::uint32_t process_id = 100,
    std::uint32_t sample_rate = 48000,
    std::uint32_t frames = 128,
    std::uint32_t inputs = 2,
    std::uint32_t outputs = 2) {
  return {
      .client_id = std::move(id),
      .process_id = process_id,
      .format = {sample_rate, frames, inputs, outputs},
  };
}

void expect_error(const sar::platform::VirtualAsioClientConnectResult& result,
                  const std::string& code) {
  assert(!result.ok());
  assert(!result.errors().empty());
  assert(result.errors().front().code == code);
}

}  // namespace

int main() {
  bool rejected_zero_capacity = false;
  try {
    sar::platform::VirtualAsioClientRegistry invalid(0);
  } catch (const std::invalid_argument&) {
    rejected_zero_capacity = true;
  }
  assert(rejected_zero_capacity);

  sar::platform::VirtualAsioClientRegistry seeded_registry(
      1, 0x123456789abcdef0ULL);
  const auto seeded = seeded_registry.connect(request("seeded", 99));
  assert(seeded.ok());
  assert(seeded.client().connection_generation == 0x123456789abcdef0ULL);

  sar::platform::VirtualAsioClientRegistry registry(2);
  assert(registry.maximum_clients() == 2);
  assert(registry.clients().empty());
  assert(!registry.active_format().has_value());

  expect_error(registry.connect(request("")), "empty_asio_client_id");
  expect_error(registry.connect(request("zero-pid", 0)),
               "invalid_asio_process_id");
  expect_error(registry.connect(request("zero-rate", 100, 0)),
               "invalid_asio_sample_rate");
  expect_error(registry.connect(request("zero-block", 100, 48000, 0)),
               "invalid_asio_block_size");
  expect_error(registry.connect(request("no-channels", 100, 48000, 128, 0, 0)),
               "empty_asio_channel_layout");
  expect_error(
      registry.connect(request("too-many-channels",
                               100,
                               48000,
                               128,
                               sar::platform::kVirtualAsioMaxChannels + 1,
                               0)),
      "asio_channel_limit_exceeded");
  expect_error(registry.connect(request(std::string(
                   sar::platform::kVirtualAsioMaxClientIdBytes + 1, 'x'))),
               "asio_client_id_too_long");

  auto reaper = registry.connect(request("reaper", 101));
  assert(reaper.ok());
  assert(reaper.client().connection_generation == 1);
  assert(registry.active_format().has_value());
  assert(registry.clients().size() == 1);

  expect_error(registry.connect(request("reaper", 999)),
               "duplicate_asio_client_id");
  expect_error(registry.connect(request("cubase-mismatch", 102, 44100)),
               "asio_session_format_mismatch");

  auto cubase = registry.connect(request("cubase", 102, 48000, 256));
  assert(cubase.ok());
  assert(cubase.client().connection_generation == 2);
  assert(cubase.client().format.frames_per_block == 256);
  assert(registry.active_format()->frames_per_block == 128);
  assert(registry.clients().size() == 2);
  expect_error(registry.connect(request("live", 103)),
               "asio_client_capacity_reached");

  const auto stale = registry.disconnect("reaper", 999);
  assert(!stale.ok());
  assert(stale.errors().front().code == "stale_asio_connection");
  assert(registry.clients().size() == 2);
  assert(registry.disconnect("reaper", 1).ok());
  assert(registry.active_format().has_value());
  assert(registry.disconnect("missing", 1).errors().front().code ==
         "unknown_asio_client");
  assert(registry.disconnect("cubase", 2).ok());
  assert(registry.clients().empty());
  assert(!registry.active_format().has_value());

  auto different_session = registry.connect(request("live", 103, 44100, 256));
  assert(different_session.ok());
  assert(different_session.client().connection_generation == 3);
  assert(registry.active_format()->sample_rate == 44100);
  assert(registry.active_format()->frames_per_block == 256);
}
