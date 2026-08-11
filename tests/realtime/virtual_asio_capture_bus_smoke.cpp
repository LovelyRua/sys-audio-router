#include "core/platform/virtual_asio_capture_bus.h"
#include "core/realtime/audio_buffer.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void fill(sar::realtime::AudioBuffer& buffer, float base) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    auto samples = buffer.channel(channel);
    for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
      samples[frame] = base + static_cast<float>(channel * 1000 + frame);
    }
  }
}

void expect_segment(const sar::realtime::AudioBuffer& buffer,
                    std::size_t destination_offset,
                    std::size_t frames,
                    float base,
                    std::size_t source_offset = 0) {
  for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
    const auto samples = buffer.channel(channel);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      const auto expected =
          base + static_cast<float>(channel * 1000 + source_offset + frame);
      assert(std::abs(samples[destination_offset + frame] - expected) < 0.0001F);
    }
  }
}

}  // namespace

int main() {
  try {
    sar::platform::VirtualAsioCaptureBus invalid(0, 128, 2, 4);
    (void)invalid;
    assert(false);
  } catch (const std::invalid_argument&) {
  }

  sar::platform::VirtualAsioCaptureBus bus(2, 128, 2, 4);
  auto first = bus.attach();
  auto second = bus.attach();
  assert(first.valid());
  assert(second.valid());
  assert(!bus.attach().valid());
  assert(bus.stats().active_consumers == 2);

  sar::realtime::AudioBuffer block_one(2, 128);
  sar::realtime::AudioBuffer block_two(2, 128);
  fill(block_one, 10.0F);
  fill(block_two, 20.0F);
  assert(bus.write(block_one));
  assert(bus.write(block_two));

  sar::realtime::AudioBuffer first_head(2, 64);
  sar::realtime::AudioBuffer first_tail(2, 192);
  assert(first.read(first_head));
  assert(first.read(first_tail));
  expect_segment(first_head, 0, 64, 10.0F);
  expect_segment(first_tail, 0, 64, 10.0F, 64);
  expect_segment(first_tail, 64, 128, 20.0F);

  sar::realtime::AudioBuffer second_full(2, 256);
  assert(second.read(second_full));
  expect_segment(second_full, 0, 128, 10.0F);
  expect_segment(second_full, 128, 128, 20.0F);

  sar::realtime::AudioBuffer empty(2, 128);
  for (std::size_t channel = 0; channel < empty.channels(); ++channel) {
    for (auto& sample : empty.channel(channel)) {
      sample = 1.0F;
    }
  }
  assert(!first.read(empty));
  for (std::size_t channel = 0; channel < empty.channels(); ++channel) {
    for (const auto sample : empty.channel(channel)) {
      assert(sample == 0.0F);
    }
  }

  first.reset();
  assert(bus.stats().active_consumers == 1);
  assert(bus.write(block_one));
  assert(second.read(empty));
  expect_segment(empty, 0, 128, 10.0F);

  second.reset();
  assert(bus.stats().active_consumers == 0);
  assert(!bus.write(block_one));

  const auto stats = bus.stats();
  assert(stats.published_blocks == 5);
  assert(stats.consumed_blocks == 5);
  assert(stats.consumer_underflows == 1);
  assert(stats.maximum_queue_depth >= 2);

  std::cout << "Virtual ASIO capture bus smoke test passed\n";
  return 0;
}
