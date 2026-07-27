#include "core/graph/graph.h"
#include "core/service/windows_virtual_asio_broker_client.h"
#include "core/service/windows_virtual_asio_broker_server.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace {

std::unique_ptr<sar::graph::Graph> make_graph(
    const sar::platform::VirtualAsioFormat& format) {
  auto graph = std::make_unique<sar::graph::Graph>(
      1, format.input_channels, format.frames_per_block, format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::GainNode>(0.25F));
  return graph;
}

void fill(sar::realtime::AudioBuffer& buffer) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      buffer.channel(channel)[frame] =
          static_cast<float>(channel * 100 + frame) / 1000.0F;
    }
  }
}

}  // namespace

int main() {
  constexpr std::uint32_t kChannels = 2;
  constexpr std::uint32_t kFrames = 32;
  sar::service::WindowsVirtualAsioTransportHost host({
      .endpoint_token = "broker-client-smoke",
      .maximum_clients = 2,
      .queue_capacity_blocks = 4,
      .wait_timeout_ms = 10,
  });
  sar::service::WindowsVirtualAsioBrokerServer server(
      L"sys-audio-route-asio-client-smoke-" +
          std::to_wstring(GetCurrentProcessId()),
      host, make_graph,
      [] { return sar::platform::VirtualAsioFormat{48000, kFrames, kChannels,
                                                   kChannels}; });
  assert(server.start().ok());

  const auto format =
      sar::service::WindowsVirtualAsioBrokerClient::query_format(
          server.pipe_config(), 700);
  assert(format.ok());
  assert(format.format ==
         sar::platform::VirtualAsioFormat(48000, kFrames, kChannels, kChannels));

  const sar::control::VirtualAsioBrokerConnectRequest request{
      .request_id = 701,
      .client_id = "asio-dll-smoke",
      .format = {48000, kFrames, kChannels, kChannels},
      .queue_capacity_blocks = 4,
      .client_nonce_low = 0x1122334455667788ULL,
      .client_nonce_high = 0x8877665544332211ULL,
  };
  auto connected = sar::service::WindowsVirtualAsioBrokerClient::connect(
      server.pipe_config(), request);
  assert(connected.ok());
  auto client = connected.take_client();
  assert(client->connected());
  assert(client->connection_generation() != 0);
  assert(client->header().client_process_id == GetCurrentProcessId());
  assert(host.active_session_count() == 1);

  sar::realtime::AudioBuffer input(kChannels, kFrames);
  sar::realtime::AudioBuffer output(kChannels, kFrames);
  fill(input);
  const sar::platform::VirtualAsioSharedBlockMetadata sent{
      .sequence = 17,
      .sample_position = 544,
      .qpc_position_100ns = 998877,
      .flags = 3,
  };
  assert(client->push_input(input, sent) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client->signal_input());
  assert(client->wait_output_or_shutdown(1000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);

  sar::platform::VirtualAsioSharedBlockMetadata received;
  assert(client->pop_output(output, received) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(received.sequence == sent.sequence);
  assert(received.sample_position == sent.sample_position);
  assert(received.qpc_position_100ns == sent.qpc_position_100ns);
  assert(received.flags == sent.flags);
  for (std::size_t channel = 0; channel < kChannels; ++channel) {
    for (std::size_t frame = 0; frame < kFrames; ++frame) {
      assert(std::fabs(output.channel(channel)[frame] -
                       input.channel(channel)[frame] * 0.25F) < 0.000001F);
    }
  }

  assert(client->disconnect().ok());
  assert(!client->connected());
  assert(host.active_session_count() == 0);
  assert(client->disconnect().ok());
  server.stop();
}
