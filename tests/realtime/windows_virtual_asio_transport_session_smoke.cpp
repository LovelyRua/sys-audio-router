#include "core/graph/graph.h"
#include "core/platform/windows_virtual_asio_shared_memory.h"
#include "core/service/windows_virtual_asio_transport_session.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace {

void fill(sar::realtime::AudioBuffer& buffer, std::uint64_t sequence) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      buffer.channel(channel)[frame] =
          static_cast<float>(sequence * 100 + channel * 10 + frame) / 1000.0F;
    }
  }
}

void expect_half_gain(const sar::realtime::AudioBuffer& input,
                      const sar::realtime::AudioBuffer& output) {
  assert(input.channels() == output.channels());
  assert(input.frames() == output.frames());
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      assert(std::fabs(output.channel(channel)[frame] -
                       input.channel(channel)[frame] * 0.5F) < 0.000001F);
    }
  }
}

}  // namespace

int main() {
  using namespace sar::platform;
  using sar::service::WindowsVirtualAsioTransportSession;

  constexpr std::uint32_t kFrames = 32;
  constexpr std::uint32_t kChannels = 2;
  constexpr std::uint64_t kGeneration = 41;
  const auto name_result = make_windows_virtual_asio_object_names(
      "transport-smoke", "client", kGeneration);
  assert(name_result.ok());
  const auto names = name_result.names();
  const VirtualAsioSharedMemoryConfig config{
      .format = {48000, kFrames, kChannels, kChannels},
      .queue_capacity_blocks = 8,
  };
  const VirtualAsioSharedMemoryIdentity identity{
      .connection_generation = kGeneration,
      .owner_process_id = GetCurrentProcessId(),
      .client_process_id = GetCurrentProcessId(),
      .server_nonce_low = 11,
      .server_nonce_high = 12,
      .client_nonce_low = 21,
      .client_nonce_high = 22,
  };

  auto bad_graph = std::make_unique<sar::graph::Graph>(1, 1, kFrames, 48000);
  auto bad = WindowsVirtualAsioTransportSession::create(
      names, config, identity, std::move(bad_graph));
  assert(!bad.ok());
  assert(bad.errors().front().code ==
         "virtual_asio_session_graph_format_mismatch");

  auto graph = std::make_unique<sar::graph::Graph>(2, kChannels, kFrames, 48000);
  graph->add_node(std::make_unique<sar::graph::GainNode>(0.5F));
  auto created = WindowsVirtualAsioTransportSession::create(
      names, config, identity, std::move(graph), 20);
  assert(created.ok());
  auto session = created.take_session();
  assert(session->shared_state() == VirtualAsioSharedMemoryState::Initializing);

  auto mapping_result = WindowsVirtualAsioSharedMemory::open(names.mapping);
  assert(mapping_result.ok());
  auto client_mapping = mapping_result.take_mapping();
  auto events_result = WindowsVirtualAsioEvents::open(names);
  assert(events_result.ok());
  auto client_events = events_result.take_events();
  auto input_result = WindowsVirtualAsioSharedQueue::bind(
      *client_mapping, VirtualAsioSharedQueueDirection::Input);
  auto output_result = WindowsVirtualAsioSharedQueue::bind(
      *client_mapping, VirtualAsioSharedQueueDirection::Output);
  assert(input_result.ok());
  assert(output_result.ok());
  auto input = input_result.take_queue();
  auto output = output_result.take_queue();

  assert(session->start());
  assert(session->running());
  assert(client_mapping->state() == VirtualAsioSharedMemoryState::Ready);

  sar::realtime::AudioBuffer source(kChannels, kFrames);
  sar::realtime::AudioBuffer destination(kChannels, kFrames);
  constexpr std::uint64_t kBlocks = 128;
  for (std::uint64_t sequence = 0; sequence < kBlocks; ++sequence) {
    fill(source, sequence);
    const VirtualAsioSharedBlockMetadata sent{
        .sequence = sequence,
        .sample_position = sequence * kFrames,
        .qpc_position_100ns = 10000 + sequence,
        .flags = static_cast<std::uint32_t>(sequence & 1U),
    };
    assert(input.push(source, sent) == VirtualAsioSharedQueueStatus::Completed);
    assert(client_events->signal_input());
    assert(client_events->wait_output_or_shutdown(1000).status ==
           WindowsVirtualAsioEventWaitStatus::Ready);

    VirtualAsioSharedBlockMetadata received;
    assert(output.pop(destination, received) ==
           VirtualAsioSharedQueueStatus::Completed);
    assert(received.sequence == sent.sequence);
    assert(received.connection_generation == kGeneration);
    assert(received.sample_position == sent.sample_position);
    assert(received.qpc_position_100ns == sent.qpc_position_100ns);
    assert(received.flags == sent.flags);
    expect_half_gain(source, destination);
  }

  const auto before_stop = std::chrono::steady_clock::now();
  session->stop();
  assert(std::chrono::steady_clock::now() - before_stop <
         std::chrono::seconds(2));
  assert(!session->running());
  assert(!session->start());
  assert(client_mapping->state() == VirtualAsioSharedMemoryState::Stopping);

  const auto stats = session->stats();
  assert(stats.processed_blocks == kBlocks);
  assert(stats.dropped_output_blocks == 0);
  assert(stats.input_queue_errors == 0);
  assert(stats.output_queue_errors == 0);
  assert(stats.wait_failures == 0);
  assert(stats.output_signal_failures == 0);
  assert(stats.last_sequence == kBlocks - 1);
}
