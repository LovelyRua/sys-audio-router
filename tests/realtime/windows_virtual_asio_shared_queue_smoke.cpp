#include "core/platform/windows_virtual_asio_shared_queue.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

namespace {

std::wstring unique_name(const wchar_t* suffix) {
  return L"Local\\SAR.VirtualASIO.v1.queue-test." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(GetTickCount64()) + L"." + suffix;
}

sar::platform::VirtualAsioSharedMemoryIdentity identity(
    std::uint64_t generation) {
  return {
      .connection_generation = generation,
      .owner_process_id = GetCurrentProcessId(),
      .client_process_id = GetCurrentProcessId(),
      .server_nonce_low = 1,
      .server_nonce_high = 2,
      .client_nonce_low = 3,
      .client_nonce_high = 4,
  };
}

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    auto samples = buffer.channel(channel);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      samples[frame] = base + static_cast<float>(channel * 1000 + frame);
    }
  }
}

void expect_equal(const sar::realtime::AudioBuffer& left,
                  const sar::realtime::AudioBuffer& right) {
  assert(left.channels() == right.channels());
  assert(left.frames() == right.frames());
  for (std::size_t channel = 0; channel < left.channels(); ++channel) {
    for (std::size_t frame = 0; frame < left.frames(); ++frame) {
      assert(left.channel(channel)[frame] == right.channel(channel)[frame]);
    }
  }
}

void expect_silence(const sar::realtime::AudioBuffer& buffer) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    for (const auto sample : buffer.channel(channel)) {
      assert(sample == 0.0F);
    }
  }
}

}  // namespace

int main() {
  using namespace sar::platform;
  constexpr std::uint32_t kFrames = 32;
  const VirtualAsioSharedMemoryConfig configuration{
      .format = {48000, kFrames, 2, 2},
      .queue_capacity_blocks = 2,
  };
  auto created = WindowsVirtualAsioSharedMemory::create(
      unique_name(L"behavior"), configuration, identity(11));
  assert(created.ok());
  auto owner = created.take_mapping();
  auto opened = WindowsVirtualAsioSharedMemory::open(owner->object_name());
  assert(opened.ok());
  auto client = opened.take_mapping();

  auto input_producer_result = WindowsVirtualAsioSharedQueue::bind(
      *client, VirtualAsioSharedQueueDirection::Input);
  auto input_consumer_result = WindowsVirtualAsioSharedQueue::bind(
      *owner, VirtualAsioSharedQueueDirection::Input);
  auto output_producer_result = WindowsVirtualAsioSharedQueue::bind(
      *owner, VirtualAsioSharedQueueDirection::Output);
  auto output_consumer_result = WindowsVirtualAsioSharedQueue::bind(
      *client, VirtualAsioSharedQueueDirection::Output);
  assert(input_producer_result.ok() && input_consumer_result.ok());
  assert(output_producer_result.ok() && output_consumer_result.ok());
  auto input_producer = input_producer_result.take_queue();
  auto input_consumer = input_consumer_result.take_queue();
  auto output_producer = output_producer_result.take_queue();
  auto output_consumer = output_consumer_result.take_queue();
  assert(input_producer.channels() == 2);
  assert(input_producer.frames_per_block() == kFrames);
  assert(input_producer.capacity_blocks() == 2);

  sar::realtime::AudioBuffer source(2, kFrames);
  sar::realtime::AudioBuffer destination(2, kFrames);
  fill(source, 10.0F);
  VirtualAsioSharedBlockMetadata metadata{
      .sequence = 4,
      .sample_position = 128,
      .qpc_position_100ns = 9000,
      .flags = 3,
  };
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::NotReady);
  owner->set_state(VirtualAsioSharedMemoryState::Ready);

  sar::realtime::AudioBuffer wrong_format(1, kFrames);
  assert(input_producer.push(wrong_format, metadata) ==
         VirtualAsioSharedQueueStatus::FormatMismatch);
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  VirtualAsioSharedBlockMetadata received;
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);
  expect_equal(source, destination);
  assert(received.sequence == 4);
  assert(received.connection_generation == 11);
  assert(received.sample_position == 128);
  assert(received.qpc_position_100ns == 9000);
  assert(received.flags == 3);

  fill(destination, 99.0F);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Empty);
  expect_silence(destination);

  metadata.sequence = 5;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  metadata.sequence = 6;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  metadata.sequence = 7;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Full);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);

  metadata.sequence = 20;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(received.sequence == 20);

  metadata.sequence = 21;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  auto positions = input_consumer.stats();
  auto* base = static_cast<std::byte*>(owner->data());
  auto* stale_header = reinterpret_cast<VirtualAsioSharedBlockHeader*>(
      base + owner->layout().input_queue.slots_offset +
      (positions.read_position % input_consumer.capacity_blocks()) *
          owner->layout().input_queue.slot_stride);
  stale_header->connection_generation = 999;
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::StaleGeneration);
  expect_silence(destination);

  metadata.sequence = 22;
  assert(input_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  positions = input_consumer.stats();
  auto* corrupt_header = reinterpret_cast<VirtualAsioSharedBlockHeader*>(
      base + owner->layout().input_queue.slots_offset +
      (positions.read_position % input_consumer.capacity_blocks()) *
          owner->layout().input_queue.slot_stride);
  corrupt_header->valid_frames = kFrames - 1;
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::CorruptSlot);

  metadata.sequence = 1;
  assert(output_producer.push(source, metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(output_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);
  expect_equal(source, destination);

  const auto input_stats = input_consumer.stats();
  assert(input_stats.produced_blocks == 6);
  assert(input_stats.consumed_blocks == 6);
  assert(input_stats.overrun_blocks == 1);
  assert(input_stats.underrun_blocks == 1);
  assert(input_stats.sequence_discontinuities >= 3);

  const VirtualAsioSharedMemoryConfig stress_configuration{
      .format = {48000, kFrames, 2, 0},
      .queue_capacity_blocks = 32,
  };
  auto stress_created = WindowsVirtualAsioSharedMemory::create(
      unique_name(L"threaded"), stress_configuration, identity(12));
  assert(stress_created.ok());
  auto stress_owner = stress_created.take_mapping();
  auto stress_opened =
      WindowsVirtualAsioSharedMemory::open(stress_owner->object_name());
  assert(stress_opened.ok());
  auto stress_client = stress_opened.take_mapping();
  auto producer = WindowsVirtualAsioSharedQueue::bind(
                      *stress_client, VirtualAsioSharedQueueDirection::Input)
                      .take_queue();
  auto consumer = WindowsVirtualAsioSharedQueue::bind(
                      *stress_owner, VirtualAsioSharedQueueDirection::Input)
                      .take_queue();
  stress_owner->set_state(VirtualAsioSharedMemoryState::Ready);

  constexpr std::uint64_t kBlocks = 20000;
  std::atomic_bool producer_done = false;
  std::thread producer_thread([&] {
    sar::realtime::AudioBuffer block(2, kFrames);
    fill(block, 123.0F);
    for (std::uint64_t sequence = 0; sequence < kBlocks;) {
      VirtualAsioSharedBlockMetadata block_metadata{.sequence = sequence};
      const auto status = producer.push(block, block_metadata);
      if (status == VirtualAsioSharedQueueStatus::Completed) {
        ++sequence;
      } else {
        assert(status == VirtualAsioSharedQueueStatus::Full);
        std::this_thread::yield();
      }
    }
    producer_done.store(true);
  });
  sar::realtime::AudioBuffer block(2, kFrames);
  std::uint64_t consumed = 0;
  while (consumed < kBlocks) {
    VirtualAsioSharedBlockMetadata block_metadata;
    const auto status = consumer.pop(block, block_metadata);
    if (status == VirtualAsioSharedQueueStatus::Completed) {
      assert(block_metadata.sequence == consumed);
      ++consumed;
    } else {
      assert(status == VirtualAsioSharedQueueStatus::Empty);
      if (!producer_done.load()) {
        std::this_thread::yield();
      }
    }
  }
  producer_thread.join();
  const auto stress_stats = consumer.stats();
  assert(stress_stats.produced_blocks == kBlocks);
  assert(stress_stats.consumed_blocks == kBlocks);
  assert(stress_stats.sequence_discontinuities == 0);
}
