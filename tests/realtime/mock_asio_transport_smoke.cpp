#include "core/platform/mock_asio_transport.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace {

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    auto samples = buffer.channel(channel);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      samples[frame] = base + static_cast<float>(channel * 100 + frame);
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

}  // namespace

int main() {
  constexpr std::size_t kChannels = 2;
  constexpr std::size_t kFrames = 32;
  sar::platform::MockAsioTransport transport(kChannels, kFrames, 2);
  assert(transport.channels() == kChannels);
  assert(transport.frames_per_block() == kFrames);
  assert(transport.queue_capacity_blocks() == 2);
  assert(transport.connection_generation() == 1);

  sar::realtime::AudioBuffer source(kChannels, kFrames);
  sar::realtime::AudioBuffer destination(kChannels, kFrames);
  fill(source, 10.0F);
  sar::platform::MockAsioBlockMetadata metadata;
  assert(transport.client_push_input(source, 0));
  assert(transport.engine_pop_input(destination, metadata));
  assert(metadata.sequence == 0 && metadata.generation == 1);
  expect_equal(source, destination);

  assert(!transport.engine_pop_input(destination, metadata));
  assert(transport.stats().input_underruns == 1);

  assert(transport.engine_push_output(source, 0));
  assert(transport.client_pop_output(destination, metadata));
  expect_equal(source, destination);
  assert(!transport.client_pop_output(destination, metadata));
  assert(transport.stats().output_underruns == 1);

  assert(transport.client_push_input(source, 2));
  assert(transport.engine_pop_input(destination, metadata));
  assert(transport.stats().input_sequence_discontinuities == 1);

  assert(transport.client_push_input(source, 3));
  assert(transport.client_push_input(source, 4));
  assert(!transport.client_push_input(source, 5));
  assert(transport.stats().dropped_input_blocks == 1);

  transport.reset_connection(9);
  assert(transport.connection_generation() == 9);
  assert(!transport.engine_pop_input(destination, metadata));

  constexpr std::uint64_t kThreadedBlocks = 20000;
  sar::platform::MockAsioTransport threaded(kChannels, kFrames, 64);
  std::atomic_bool producer_done = false;
  std::thread producer([&] {
    sar::realtime::AudioBuffer block(kChannels, kFrames);
    for (std::uint64_t sequence = 0; sequence < kThreadedBlocks;) {
      fill(block, static_cast<float>(sequence));
      if (threaded.client_push_input(block, sequence)) {
        ++sequence;
      } else {
        std::this_thread::yield();
      }
    }
    producer_done.store(true);
  });
  std::uint64_t consumed = 0;
  sar::realtime::AudioBuffer block(kChannels, kFrames);
  while (consumed < kThreadedBlocks) {
    if (threaded.engine_pop_input(block, metadata)) {
      assert(metadata.sequence == consumed);
      ++consumed;
    } else if (!producer_done.load()) {
      std::this_thread::yield();
    }
  }
  producer.join();
  assert(threaded.stats().input_sequence_discontinuities == 0);
}
