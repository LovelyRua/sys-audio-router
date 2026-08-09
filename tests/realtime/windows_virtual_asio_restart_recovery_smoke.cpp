#include "core/graph/graph.h"
#include "core/service/windows_virtual_asio_broker_client.h"
#include "core/service/windows_virtual_asio_broker_server.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace {

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kFrames = 32;
constexpr float kGain = 0.5F;

std::unique_ptr<sar::graph::Graph> make_graph(
    const sar::platform::VirtualAsioFormat& format) {
  auto graph = std::make_unique<sar::graph::Graph>(
      1, format.input_channels, format.frames_per_block, format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::GainNode>(kGain));
  return graph;
}

sar::control::VirtualAsioBrokerConnectRequest make_request(
    std::uint64_t request_id,
    std::uint64_t nonce) {
  return {
      .request_id = request_id,
      .client_id = "restart-recovery-client",
      .format = {48000, kFrames, kChannels, kChannels},
      .queue_capacity_blocks = 4,
      .client_nonce_low = nonce,
      .client_nonce_high = ~nonce,
  };
}

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      buffer.channel(channel)[frame] =
          base + static_cast<float>(channel * kFrames + frame) / 1000.0F;
    }
  }
}

void expect_stream_round_trip(
    sar::service::WindowsVirtualAsioBrokerClient& client,
    std::uint64_t sequence,
    float base) {
  sar::realtime::AudioBuffer input(kChannels, kFrames);
  sar::realtime::AudioBuffer output(kChannels, kFrames);
  fill(input, base);

  const sar::platform::VirtualAsioSharedBlockMetadata sent{
      .sequence = sequence,
      .sample_position = sequence * kFrames,
      .qpc_position_100ns = sequence * 1000,
  };
  assert(client.push_input(input, sent) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client.signal_input());
  assert(client.wait_output_or_shutdown(1000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);

  sar::platform::VirtualAsioSharedBlockMetadata received;
  assert(client.pop_output(output, received) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(received.sequence == sent.sequence);
  assert(received.sample_position == sent.sample_position);
  assert(received.qpc_position_100ns == sent.qpc_position_100ns);
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
      assert(std::fabs(output.channel(channel)[frame] -
                       input.channel(channel)[frame] * kGain) < 0.000001F);
    }
  }
}

}  // namespace

int main() {
  using namespace std::chrono_literals;

  const std::wstring pipe_name =
      L"sys-audio-route-asio-restart-smoke-" +
      std::to_wstring(GetCurrentProcessId());
  std::unique_ptr<sar::service::WindowsVirtualAsioBrokerClient> old_client;
  std::wstring old_mapping_name;
  std::uint64_t old_connection_generation = 0;

  {
    sar::service::WindowsVirtualAsioTransportHost host({
        .endpoint_token = "restart-recovery-smoke",
        .maximum_clients = 1,
        .queue_capacity_blocks = 4,
        .wait_timeout_ms = 10,
    });
    sar::service::WindowsVirtualAsioBrokerServer server(
        pipe_name, host, make_graph,
        [] { return sar::platform::VirtualAsioFormat{48000, kFrames, kChannels,
                                                     kChannels}; });
    assert(server.start().ok());

    auto connected = sar::service::WindowsVirtualAsioBrokerClient::connect(
        server.pipe_config(), make_request(1001, 0x1111222233334444ULL));
    assert(connected.ok());
    old_client = connected.take_client();
    assert(old_client->connected());
    assert(host.active_session_count() == 1);
    old_mapping_name = old_client->names().mapping;
    old_connection_generation = old_client->connection_generation();
    expect_stream_round_trip(*old_client, 1, 0.1F);

    auto shutdown_wait = std::async(std::launch::async, [&] {
      return old_client->wait_output_or_shutdown(5000);
    });
    server.stop();
    host.stop_all();

    assert(shutdown_wait.wait_for(1s) == std::future_status::ready);
    assert(shutdown_wait.get().status ==
           sar::platform::WindowsVirtualAsioEventWaitStatus::Shutdown);
    assert(host.active_session_count() == 0);

    auto disconnect_wait = std::async(std::launch::async, [&] {
      return old_client->disconnect(100);
    });
    assert(disconnect_wait.wait_for(1s) == std::future_status::ready);
    const auto disconnected = disconnect_wait.get();
    assert(!disconnected.ok());
    assert(!old_client->connected());
    assert(old_client->push_input(
               sar::realtime::AudioBuffer(kChannels, kFrames), {}) ==
           sar::platform::VirtualAsioSharedQueueStatus::NotReady);
    assert(!old_client->signal_input());
  }

  {
    sar::service::WindowsVirtualAsioTransportHost host({
        .endpoint_token = "restart-recovery-smoke",
        .maximum_clients = 1,
        .queue_capacity_blocks = 4,
        .wait_timeout_ms = 10,
    });
    sar::service::WindowsVirtualAsioBrokerServer server(
        pipe_name, host, make_graph,
        [] { return sar::platform::VirtualAsioFormat{48000, kFrames, kChannels,
                                                     kChannels}; });
    assert(server.start().ok());

    auto connected = sar::service::WindowsVirtualAsioBrokerClient::connect(
        server.pipe_config(), make_request(2001, 0xAAAABBBBCCCCDDDDULL));
    assert(connected.ok());
    auto fresh_client = connected.take_client();
    assert(fresh_client->connected());
    assert(fresh_client->connection_generation() !=
           old_connection_generation);
    assert(fresh_client->names().mapping != old_mapping_name);
    assert(host.active_session_count() == 1);
    const auto stale_disconnect = host.disconnect(
        "restart-recovery-client", old_connection_generation);
    assert(!stale_disconnect.ok());
    assert(stale_disconnect.errors().front().code ==
           "stale_asio_connection");
    assert(host.active_session_count() == 1);
    expect_stream_round_trip(*fresh_client, 2, 0.6F);

    assert(fresh_client->disconnect().ok());
    assert(!fresh_client->connected());
    assert(host.active_session_count() == 0);
    server.stop();
  }

  old_client.reset();
}
