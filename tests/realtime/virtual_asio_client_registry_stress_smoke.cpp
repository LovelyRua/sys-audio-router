#include "core/platform/virtual_asio_client_registry.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using sar::platform::VirtualAsioClientConnectResult;
using sar::platform::VirtualAsioClientDisconnectResult;
using sar::platform::VirtualAsioClientRegistry;
using sar::platform::VirtualAsioClientRequest;
using sar::platform::VirtualAsioFormat;

VirtualAsioClientRequest request(std::string client_id,
                                 std::uint32_t process_id,
                                 VirtualAsioFormat format) {
  return {
      .client_id = std::move(client_id),
      .process_id = process_id,
      .format = format,
  };
}

void expect_connect_error(const VirtualAsioClientConnectResult& result,
                          const char* code) {
  assert(!result.ok());
  assert(!result.errors().empty());
  assert(result.errors().front().code == code);
}

void expect_disconnect_error(const VirtualAsioClientDisconnectResult& result,
                             const char* code) {
  assert(!result.ok());
  assert(!result.errors().empty());
  assert(result.errors().front().code == code);
}

void stress_generation_and_stale_disconnect() {
  constexpr std::size_t kReconnectCount = 20000;
  const VirtualAsioFormat format{48000, 128, 2, 2};
  VirtualAsioClientRegistry registry(1);

  std::uint64_t previous_generation = 0;
  for (std::size_t iteration = 0; iteration < kReconnectCount; ++iteration) {
    auto connected = registry.connect(
        request("reused-client", static_cast<std::uint32_t>(iteration + 1), format));
    assert(connected.ok());

    const auto generation = connected.client().connection_generation;
    assert(generation != 0);
    assert(generation > previous_generation);
    assert(registry.clients().size() == 1);

    if (previous_generation != 0) {
      expect_disconnect_error(
          registry.disconnect("reused-client", previous_generation),
          "stale_asio_connection");
      assert(registry.clients().size() == 1);
      assert(registry.clients().front().connection_generation == generation);
    }

    assert(registry.disconnect("reused-client", generation).ok());
    assert(registry.clients().empty());
    assert(!registry.active_format().has_value());
    previous_generation = generation;
  }
}

void test_single_direction_channel_layouts() {
  const VirtualAsioFormat input_only{48000, 64, 8, 0};
  const VirtualAsioFormat output_only{96000, 256, 0, 16};
  VirtualAsioClientRegistry registry(2);

  auto capture = registry.connect(request("capture-only", 1001, input_only));
  assert(capture.ok());
  assert(capture.client().format.input_channels == 8);
  assert(capture.client().format.output_channels == 0);
  assert(registry.active_format() == input_only);
  assert(registry.disconnect("capture-only",
                             capture.client().connection_generation)
             .ok());
  assert(!registry.active_format().has_value());

  auto render = registry.connect(request("render-only", 1002, output_only));
  assert(render.ok());
  assert(render.client().format.input_channels == 0);
  assert(render.client().format.output_channels == 16);
  assert(registry.active_format() == output_only);
  assert(registry.disconnect("render-only",
                             render.client().connection_generation)
             .ok());
}

void test_format_lock_lifetime() {
  const VirtualAsioFormat first_format{48000, 128, 2, 2};
  const VirtualAsioFormat second_format{44100, 512, 0, 2};
  VirtualAsioClientRegistry registry(3);

  auto first = registry.connect(request("first", 2001, first_format));
  auto peer = registry.connect(request("peer", 2002, first_format));
  assert(first.ok());
  assert(peer.ok());
  assert(registry.active_format() == first_format);

  expect_connect_error(registry.connect(request("mismatch", 2003, second_format)),
                       "asio_session_format_mismatch");
  assert(registry.clients().size() == 2);
  assert(registry.active_format() == first_format);

  assert(registry.disconnect("first", first.client().connection_generation).ok());
  assert(registry.active_format() == first_format);
  expect_connect_error(registry.connect(request("still-mismatch", 2004, second_format)),
                       "asio_session_format_mismatch");

  assert(registry.disconnect("peer", peer.client().connection_generation).ok());
  assert(!registry.active_format().has_value());

  auto replacement =
      registry.connect(request("replacement", 2005, second_format));
  assert(replacement.ok());
  assert(registry.active_format() == second_format);
  assert(replacement.client().connection_generation >
         peer.client().connection_generation);
}

void stress_capacity_reuse() {
  constexpr std::size_t kCapacity = 32;
  constexpr std::size_t kCycles = 500;
  const VirtualAsioFormat format{48000, 64, 16, 16};
  VirtualAsioClientRegistry registry(kCapacity);
  std::uint64_t previous_generation = 0;

  for (std::size_t cycle = 0; cycle < kCycles; ++cycle) {
    std::vector<std::uint64_t> generations;
    generations.reserve(kCapacity);

    for (std::size_t slot = 0; slot < kCapacity; ++slot) {
      auto connected = registry.connect(request(
          "capacity-client-" + std::to_string(slot),
          static_cast<std::uint32_t>(3000 + slot),
          format));
      assert(connected.ok());
      assert(connected.client().connection_generation > previous_generation);
      previous_generation = connected.client().connection_generation;
      generations.push_back(previous_generation);
    }

    assert(registry.clients().size() == kCapacity);
    expect_connect_error(
        registry.connect(request("over-capacity", 9999, format)),
        "asio_client_capacity_reached");

    for (std::size_t slot = 0; slot < kCapacity; slot += 2) {
      assert(registry
                 .disconnect("capacity-client-" + std::to_string(slot),
                             generations[slot])
                 .ok());
    }
    assert(registry.clients().size() == kCapacity / 2);
    assert(registry.active_format() == format);

    for (std::size_t slot = 0; slot < kCapacity; slot += 2) {
      auto reconnected = registry.connect(request(
          "capacity-client-" + std::to_string(slot),
          static_cast<std::uint32_t>(6000 + slot),
          format));
      assert(reconnected.ok());
      assert(reconnected.client().connection_generation > previous_generation);
      previous_generation = reconnected.client().connection_generation;

      expect_disconnect_error(
          registry.disconnect("capacity-client-" + std::to_string(slot),
                              generations[slot]),
          "stale_asio_connection");
      generations[slot] = reconnected.client().connection_generation;
    }
    assert(registry.clients().size() == kCapacity);

    for (std::size_t slot = 0; slot < kCapacity; ++slot) {
      assert(registry
                 .disconnect("capacity-client-" + std::to_string(slot),
                             generations[slot])
                 .ok());
    }
    assert(registry.clients().empty());
    assert(!registry.active_format().has_value());
  }
}

}  // namespace

int main() {
  stress_generation_and_stale_disconnect();
  test_single_direction_channel_layouts();
  test_format_lock_lifetime();
  stress_capacity_reuse();
}
