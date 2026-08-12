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

void expect_gain(const sar::realtime::AudioBuffer& input,
                 const sar::realtime::AudioBuffer& output,
                 float gain) {
  assert(input.channels() == output.channels());
  assert(input.frames() == output.frames());
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      assert(std::fabs(output.channel(channel)[frame] -
                       input.channel(channel)[frame] * gain) < 0.000001F);
    }
  }
}

void expect_equal(const sar::realtime::AudioBuffer& expected,
                  const sar::realtime::AudioBuffer& actual) {
  expect_gain(expected, actual, 1.0F);
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
    if (sequence == kBlocks / 2) {
      auto incompatible =
          std::make_unique<sar::graph::Graph>(3, 1, kFrames, 48000);
      assert(!session->replace_graph(std::move(incompatible)));
      auto replacement =
          std::make_unique<sar::graph::Graph>(3, kChannels, kFrames, 48000);
      replacement->add_node(std::make_unique<sar::graph::GainNode>(0.25F));
      assert(session->replace_graph(std::move(replacement)));
    }
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
    expect_gain(source, destination,
                sequence < kBlocks / 2 ? 0.5F : 0.25F);
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
  assert(stats.client_disconnects == 0);
  assert(stats.last_sequence == kBlocks - 1);
  assert(stats.graph_updates == 1);
  assert(stats.current_graph_version == 3);

  const auto central_name_result = make_windows_virtual_asio_object_names(
      "transport-smoke", "central-client", kGeneration + 10);
  assert(central_name_result.ok());
  auto central_identity = identity;
  central_identity.connection_generation = kGeneration + 10;
  VirtualAsioRenderBus render_bus(kChannels, kFrames, 1, 4);
  VirtualAsioCaptureBus capture_bus(kChannels, kFrames, 1, 4);
  auto central_graph =
      std::make_unique<sar::graph::Graph>(4, kChannels, kFrames, 48000);
  central_graph->add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  auto central_created = WindowsVirtualAsioTransportSession::create(
      central_name_result.names(), config, central_identity,
      std::move(central_graph), 20, render_bus.attach(), capture_bus.attach());
  assert(central_created.ok());
  auto central_session = central_created.take_session();
  auto central_mapping_result = WindowsVirtualAsioSharedMemory::open(
      central_name_result.names().mapping);
  assert(central_mapping_result.ok());
  auto central_mapping = central_mapping_result.take_mapping();
  auto central_events_result =
      WindowsVirtualAsioEvents::open(central_name_result.names());
  assert(central_events_result.ok());
  auto central_events = central_events_result.take_events();
  auto central_input_result = WindowsVirtualAsioSharedQueue::bind(
      *central_mapping, VirtualAsioSharedQueueDirection::Input);
  auto central_output_result = WindowsVirtualAsioSharedQueue::bind(
      *central_mapping, VirtualAsioSharedQueueDirection::Output);
  assert(central_input_result.ok() && central_output_result.ok());
  auto central_input = central_input_result.take_queue();
  auto central_output = central_output_result.take_queue();
  assert(central_session->start());

  sar::realtime::AudioBuffer daw_output(kChannels, kFrames);
  sar::realtime::AudioBuffer daw_input(kChannels, kFrames);
  sar::realtime::AudioBuffer matrix_output(kChannels, kFrames);
  sar::realtime::AudioBuffer rendered_from_daw(kChannels, kFrames);
  fill(daw_output, 7);
  fill(matrix_output, 9);
  assert(capture_bus.write(matrix_output));
  const VirtualAsioSharedBlockMetadata central_metadata{.sequence = 77};
  assert(central_input.push(daw_output, central_metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(central_events->signal_input());
  assert(central_events->wait_output_or_shutdown(1000).status ==
         WindowsVirtualAsioEventWaitStatus::Ready);
  VirtualAsioSharedBlockMetadata central_received;
  assert(central_output.pop(daw_input, central_received) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(central_received.sequence == central_metadata.sequence);
  expect_equal(matrix_output, daw_input);
  assert(render_bus.read(rendered_from_daw));
  expect_equal(daw_output, rendered_from_daw);
  assert(central_session->stats().capture_bus_underflows == 0);
  central_session->stop();
  central_session.reset();
  assert(capture_bus.stats().active_consumers == 0);
  assert(render_bus.diagnostics().active_producers == 0);

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION child{};
  wchar_t command[] = L"cmd.exe /c exit 0";
  assert(CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                        &startup, &child));

  const auto crash_name_result = make_windows_virtual_asio_object_names(
      "transport-smoke", "crash-client", kGeneration + 1);
  assert(crash_name_result.ok());
  auto crash_identity = identity;
  crash_identity.connection_generation = kGeneration + 1;
  crash_identity.client_process_id = child.dwProcessId;
  auto crash_graph =
      std::make_unique<sar::graph::Graph>(3, kChannels, kFrames, 48000);
  crash_graph->add_node(std::make_unique<sar::graph::PassthroughNode>());
  auto crash_created = WindowsVirtualAsioTransportSession::create(
      crash_name_result.names(), config, crash_identity,
      std::move(crash_graph), 10);
  assert(crash_created.ok());
  auto crash_session = crash_created.take_session();
  assert(crash_session->start());
  assert(ResumeThread(child.hThread) != static_cast<DWORD>(-1));
  assert(WaitForSingleObject(child.hProcess, 2000) == WAIT_OBJECT_0);

  const auto exit_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (crash_session->running() &&
         std::chrono::steady_clock::now() < exit_deadline) {
    Sleep(1);
  }
  assert(!crash_session->running());
  crash_session->stop();
  assert(crash_session->shared_state() ==
         VirtualAsioSharedMemoryState::Stopping);
  assert(crash_session->stats().client_process_exits == 1);
  CloseHandle(child.hThread);
  CloseHandle(child.hProcess);

  const auto disconnect_name_result = make_windows_virtual_asio_object_names(
      "transport-smoke", "disconnect-client", kGeneration + 2);
  assert(disconnect_name_result.ok());
  auto disconnect_identity = identity;
  disconnect_identity.connection_generation = kGeneration + 2;
  auto disconnect_graph =
      std::make_unique<sar::graph::Graph>(4, kChannels, kFrames, 48000);
  disconnect_graph->add_node(std::make_unique<sar::graph::PassthroughNode>());
  auto disconnect_created = WindowsVirtualAsioTransportSession::create(
      disconnect_name_result.names(), config, disconnect_identity,
      std::move(disconnect_graph), 1000);
  assert(disconnect_created.ok());
  auto disconnect_session = disconnect_created.take_session();
  auto disconnect_events_result =
      WindowsVirtualAsioEvents::open(disconnect_name_result.names());
  assert(disconnect_events_result.ok());
  auto disconnect_events = disconnect_events_result.take_events();
  assert(disconnect_session->start());
  assert(disconnect_events->signal_client_disconnect());
  const auto disconnect_deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (disconnect_session->running() &&
         std::chrono::steady_clock::now() < disconnect_deadline) {
    Sleep(1);
  }
  assert(!disconnect_session->running());
  assert(disconnect_session->shared_state() ==
         VirtualAsioSharedMemoryState::Stopping);
  assert(disconnect_session->stats().client_disconnects == 1);
  disconnect_session->stop();
}
