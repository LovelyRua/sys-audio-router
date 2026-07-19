#include "core/platform/windows_virtual_asio_shared_queue.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace {

using sar::platform::VirtualAsioSharedBlockHeader;
using sar::platform::VirtualAsioSharedBlockMetadata;
using sar::platform::VirtualAsioSharedMemoryIdentity;
using sar::platform::VirtualAsioSharedQueueControl;
using sar::platform::VirtualAsioSharedQueueLayout;
using sar::platform::VirtualAsioSharedQueueStats;
using sar::platform::WindowsVirtualAsioSharedMemory;

std::wstring unique_name() {
  return L"Local\\SAR.VirtualASIO.v1.queue-adversarial-test." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(GetTickCount64());
}

VirtualAsioSharedMemoryIdentity identity(std::uint64_t generation) {
  return {
      .connection_generation = generation,
      .owner_process_id = GetCurrentProcessId(),
      .client_process_id = GetCurrentProcessId(),
      .server_nonce_low = 0x101,
      .server_nonce_high = 0x102,
      .client_nonce_low = 0x201,
      .client_nonce_high = 0x202,
  };
}

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    auto samples = buffer.channel(channel);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      samples[frame] = base + static_cast<float>(channel * 100 + frame);
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

void expect_equal(const sar::realtime::AudioBuffer& expected,
                  const sar::realtime::AudioBuffer& actual) {
  assert(expected.channels() == actual.channels());
  assert(expected.frames() == actual.frames());
  for (std::size_t channel = 0; channel < expected.channels(); ++channel) {
    for (std::size_t frame = 0; frame < expected.frames(); ++frame) {
      assert(expected.channel(channel)[frame] == actual.channel(channel)[frame]);
    }
  }
}

void expect_stats(const VirtualAsioSharedQueueStats& expected,
                  const VirtualAsioSharedQueueStats& actual) {
  assert(actual.write_position == expected.write_position);
  assert(actual.read_position == expected.read_position);
  assert(actual.produced_blocks == expected.produced_blocks);
  assert(actual.consumed_blocks == expected.consumed_blocks);
  assert(actual.overrun_blocks == expected.overrun_blocks);
  assert(actual.underrun_blocks == expected.underrun_blocks);
  assert(actual.sequence_discontinuities ==
         expected.sequence_discontinuities);
}

VirtualAsioSharedQueueControl* queue_control(
    WindowsVirtualAsioSharedMemory& mapping,
    const VirtualAsioSharedQueueLayout& layout) {
  auto* base = static_cast<std::byte*>(mapping.data());
  return reinterpret_cast<VirtualAsioSharedQueueControl*>(
      base + layout.control_offset);
}

VirtualAsioSharedBlockHeader* readable_slot(
    WindowsVirtualAsioSharedMemory& mapping,
    const VirtualAsioSharedQueueLayout& layout,
    std::uint64_t read_position,
    std::uint32_t capacity) {
  auto* base = static_cast<std::byte*>(mapping.data());
  return reinterpret_cast<VirtualAsioSharedBlockHeader*>(
      base + layout.slots_offset +
      (read_position % capacity) * layout.slot_stride);
}

}  // namespace

int main() {
  using namespace sar::platform;

  constexpr std::uint32_t kFrames = 16;
  constexpr std::uint32_t kChannels = 2;
  constexpr std::uint32_t kCapacity = 4;
  constexpr std::uint64_t kGeneration = 77;
  const VirtualAsioSharedMemoryConfig configuration{
      .format = {48000, kFrames, kChannels, kChannels},
      .queue_capacity_blocks = kCapacity,
  };

  auto created = WindowsVirtualAsioSharedMemory::create(
      unique_name(), configuration, identity(kGeneration));
  assert(created.ok());
  auto owner = created.take_mapping();
  auto opened = WindowsVirtualAsioSharedMemory::open(owner->object_name());
  assert(opened.ok());
  auto client = opened.take_mapping();

  auto input_producer = WindowsVirtualAsioSharedQueue::bind(
                            *client, VirtualAsioSharedQueueDirection::Input)
                            .take_queue();
  auto input_consumer = WindowsVirtualAsioSharedQueue::bind(
                            *owner, VirtualAsioSharedQueueDirection::Input)
                            .take_queue();
  auto output_producer = WindowsVirtualAsioSharedQueue::bind(
                             *owner, VirtualAsioSharedQueueDirection::Output)
                             .take_queue();
  auto output_consumer = WindowsVirtualAsioSharedQueue::bind(
                             *client, VirtualAsioSharedQueueDirection::Output)
                             .take_queue();
  owner->set_state(VirtualAsioSharedMemoryState::Ready);

  sar::realtime::AudioBuffer source(kChannels, kFrames);
  sar::realtime::AudioBuffer destination(kChannels, kFrames);
  fill(source, 10.0F);

  const auto& input_layout = owner->layout().input_queue;
  auto* input_control = queue_control(*owner, input_layout);
  constexpr auto kNearWrap = std::numeric_limits<std::uint64_t>::max() - 1;
  input_control->write_position = kNearWrap;
  input_control->read_position = kNearWrap;

  for (std::uint64_t sequence = 100; sequence < 103; ++sequence) {
    const VirtualAsioSharedBlockMetadata metadata{
        .sequence = sequence,
        .sample_position = sequence * kFrames,
    };
    assert(input_producer.push(source, metadata) ==
           VirtualAsioSharedQueueStatus::Completed);
  }
  auto input_stats = input_consumer.stats();
  assert(input_stats.write_position == 1);
  assert(input_stats.read_position == kNearWrap);
  for (std::uint64_t sequence = 100; sequence < 103; ++sequence) {
    VirtualAsioSharedBlockMetadata metadata;
    assert(input_consumer.pop(destination, metadata) ==
           VirtualAsioSharedQueueStatus::Completed);
    assert(metadata.sequence == sequence);
    expect_equal(source, destination);
  }
  input_stats = input_consumer.stats();
  assert(input_stats.write_position == 1);
  assert(input_stats.read_position == 1);
  assert(input_stats.produced_blocks == 3);
  assert(input_stats.consumed_blocks == 3);

  // Neither direction of a format mismatch may claim or consume a slot.
  sar::realtime::AudioBuffer wrong_format(1, kFrames);
  const auto before_format_error = input_consumer.stats();
  const VirtualAsioSharedBlockMetadata format_metadata{.sequence = 103};
  assert(input_producer.push(wrong_format, format_metadata) ==
         VirtualAsioSharedQueueStatus::FormatMismatch);
  fill(wrong_format, 99.0F);
  VirtualAsioSharedBlockMetadata received;
  assert(input_consumer.pop(wrong_format, received) ==
         VirtualAsioSharedQueueStatus::FormatMismatch);
  expect_silence(wrong_format);
  expect_stats(before_format_error, input_consumer.stats());

  // NotReady is lifecycle backpressure, not an audio underrun.
  owner->set_state(VirtualAsioSharedMemoryState::Stopping);
  const auto before_not_ready = input_consumer.stats();
  fill(destination, 88.0F);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::NotReady);
  expect_silence(destination);
  assert(input_producer.push(source, format_metadata) ==
         VirtualAsioSharedQueueStatus::NotReady);
  expect_stats(before_not_ready, input_consumer.stats());
  owner->set_state(VirtualAsioSharedMemoryState::Ready);

  std::uint64_t next_sequence = 200;
  const auto corrupt_then_recover =
      [&](VirtualAsioSharedQueueStatus expected_status, auto tamper) {
        const VirtualAsioSharedBlockMetadata corrupt_metadata{
            .sequence = next_sequence++,
        };
        assert(input_producer.push(source, corrupt_metadata) ==
               VirtualAsioSharedQueueStatus::Completed);
        const auto before_corrupt_pop = input_consumer.stats();
        auto* header = readable_slot(*owner,
                                     input_layout,
                                     before_corrupt_pop.read_position,
                                     kCapacity);
        tamper(*header);
        fill(destination, 77.0F);
        assert(input_consumer.pop(destination, received) == expected_status);
        expect_silence(destination);
        const auto after_corrupt_pop = input_consumer.stats();
        assert(after_corrupt_pop.read_position ==
               before_corrupt_pop.read_position + 1);
        assert(after_corrupt_pop.consumed_blocks ==
               before_corrupt_pop.consumed_blocks + 1);
        assert(after_corrupt_pop.sequence_discontinuities ==
               before_corrupt_pop.sequence_discontinuities + 1);

        const VirtualAsioSharedBlockMetadata recovery_metadata{
            .sequence = next_sequence++,
        };
        assert(input_producer.push(source, recovery_metadata) ==
               VirtualAsioSharedQueueStatus::Completed);
        assert(input_consumer.pop(destination, received) ==
               VirtualAsioSharedQueueStatus::Completed);
        assert(received.sequence == recovery_metadata.sequence);
        expect_equal(source, destination);
      };

  for (std::size_t reserved_index = 0; reserved_index < 3; ++reserved_index) {
    corrupt_then_recover(VirtualAsioSharedQueueStatus::CorruptSlot,
                         [reserved_index](VirtualAsioSharedBlockHeader& header) {
                           header.reserved[reserved_index] = 1;
                         });
  }
  corrupt_then_recover(VirtualAsioSharedQueueStatus::CorruptSlot,
                       [](VirtualAsioSharedBlockHeader& header) {
                         header.valid_frames = kFrames - 1;
                       });
  corrupt_then_recover(VirtualAsioSharedQueueStatus::StaleGeneration,
                       [](VirtualAsioSharedBlockHeader& header) {
                         header.connection_generation = kGeneration + 1;
                       });

  // Input and output controls must remain completely independent.
  const auto input_before_output = input_consumer.stats();
  const auto output_before = output_consumer.stats();
  const VirtualAsioSharedBlockMetadata output_metadata{.sequence = 500};
  assert(output_producer.push(source, output_metadata) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(output_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Completed);
  assert(received.sequence == output_metadata.sequence);
  assert(output_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Empty);
  const auto output_after = output_consumer.stats();
  assert(output_after.write_position == output_before.write_position + 1);
  assert(output_after.read_position == output_before.read_position + 1);
  assert(output_after.produced_blocks == output_before.produced_blocks + 1);
  assert(output_after.consumed_blocks == output_before.consumed_blocks + 1);
  assert(output_after.underrun_blocks == output_before.underrun_blocks + 1);
  expect_stats(input_before_output, input_consumer.stats());

  const auto output_before_input_underrun = output_consumer.stats();
  const auto input_before_underrun = input_consumer.stats();
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::Empty);
  assert(input_consumer.stats().underrun_blocks ==
         input_before_underrun.underrun_blocks + 1);
  expect_stats(output_before_input_underrun, output_consumer.stats());

  // A peer-controlled position span beyond capacity must never expose an
  // unpublished slot or be silently repaired by the realtime side.
  input_control->read_position = 1000;
  input_control->write_position = 1000 + kCapacity + 1;
  fill(destination, 66.0F);
  assert(input_consumer.pop(destination, received) ==
         VirtualAsioSharedQueueStatus::CorruptControl);
  expect_silence(destination);
  assert(input_producer.push(source, format_metadata) ==
         VirtualAsioSharedQueueStatus::CorruptControl);
  assert(std::string{virtual_asio_shared_queue_status_name(
             VirtualAsioSharedQueueStatus::CorruptControl)} ==
         "corrupt-control");
}
