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

void fill_constant(sar::realtime::AudioBuffer& buffer, float value) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      buffer.channel(channel)[frame] =
          value + static_cast<float>(channel) * 0.1F;
    }
  }
}

void expect_gain(const sar::realtime::AudioBuffer& buffer, float value) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    const auto expected =
        (value + static_cast<float>(channel) * 0.1F) * 0.25F;
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      assert(std::fabs(buffer.channel(channel)[frame] - expected) <
             0.000001F);
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

  auto request_a = request;
  request_a.request_id = 801;
  request_a.client_id = "asio-daw-a";
  request_a.client_nonce_low = 0xA1;
  request_a.client_nonce_high = 0xA2;
  auto request_b = request;
  request_b.request_id = 802;
  request_b.client_id = "asio-daw-b";
  request_b.client_nonce_low = 0xB1;
  request_b.client_nonce_high = 0xB2;
  auto connected_a = sar::service::WindowsVirtualAsioBrokerClient::connect(
      server.pipe_config(), request_a);
  auto connected_b = sar::service::WindowsVirtualAsioBrokerClient::connect(
      server.pipe_config(), request_b);
  assert(connected_a.ok());
  assert(connected_b.ok());
  auto client_a = connected_a.take_client();
  auto client_b = connected_b.take_client();
  assert(host.active_session_count() == 2);
  assert(client_a->connection_generation() !=
         client_b->connection_generation());
  assert(client_a->names().mapping != client_b->names().mapping);

  sar::realtime::AudioBuffer input_a(kChannels, kFrames);
  sar::realtime::AudioBuffer input_b(kChannels, kFrames);
  sar::realtime::AudioBuffer output_a(kChannels, kFrames);
  sar::realtime::AudioBuffer output_b(kChannels, kFrames);
  fill_constant(input_a, 0.2F);
  fill_constant(input_b, 0.6F);
  const sar::platform::VirtualAsioSharedBlockMetadata sent_a{
      .sequence = 21, .sample_position = 672, .qpc_position_100ns = 1001};
  const sar::platform::VirtualAsioSharedBlockMetadata sent_b{
      .sequence = 22, .sample_position = 704, .qpc_position_100ns = 1002};
  assert(client_a->push_input(input_a, sent_a) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client_b->push_input(input_b, sent_b) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client_a->signal_input());
  assert(client_b->signal_input());
  assert(client_a->wait_output_or_shutdown(1000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);
  assert(client_b->wait_output_or_shutdown(1000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);
  sar::platform::VirtualAsioSharedBlockMetadata received_a;
  sar::platform::VirtualAsioSharedBlockMetadata received_b;
  assert(client_a->pop_output(output_a, received_a) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client_b->pop_output(output_b, received_b) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(received_a.sequence == sent_a.sequence);
  assert(received_b.sequence == sent_b.sequence);
  expect_gain(output_a, 0.2F);
  expect_gain(output_b, 0.6F);

  assert(client_a->disconnect().ok());
  assert(host.active_session_count() == 1);
  fill_constant(input_b, 0.8F);
  const sar::platform::VirtualAsioSharedBlockMetadata sent_b_after{
      .sequence = 23, .sample_position = 736, .qpc_position_100ns = 1003};
  assert(client_b->push_input(input_b, sent_b_after) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(client_b->signal_input());
  assert(client_b->wait_output_or_shutdown(1000).status ==
         sar::platform::WindowsVirtualAsioEventWaitStatus::Ready);
  assert(client_b->pop_output(output_b, received_b) ==
         sar::platform::VirtualAsioSharedQueueStatus::Completed);
  assert(received_b.sequence == sent_b_after.sequence);
  expect_gain(output_b, 0.8F);
  assert(client_b->disconnect().ok());
  assert(host.active_session_count() == 0);
  server.stop();
}
