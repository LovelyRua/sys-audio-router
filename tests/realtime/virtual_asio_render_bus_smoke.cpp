#include "core/platform/virtual_asio_render_bus.h"
#include "tests/realtime/test_helpers.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>

namespace {

void fill(sar::realtime::AudioBuffer& buffer, float left, float right) {
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    buffer.channel(0)[frame] = left;
    buffer.channel(1)[frame] = right;
  }
}

void expect(const sar::realtime::AudioBuffer& buffer,
            float left,
            float right) {
  for (std::size_t frame = 0; frame < buffer.frames(); ++frame) {
    assert(sar::tests::nearly_equal(buffer.channel(0)[frame], left));
    assert(sar::tests::nearly_equal(buffer.channel(1)[frame], right));
  }
}

}  // namespace

int main() {
  sar::platform::VirtualAsioRenderBus adapter_bus(2, 128, 1, 8);
  auto adapter = adapter_bus.attach();
  sar::realtime::AudioBuffer half_a(2, 64);
  sar::realtime::AudioBuffer half_b(2, 64);
  sar::realtime::AudioBuffer double_block(2, 256);
  sar::realtime::AudioBuffer adapted(2, 128);
  fill(half_a, 0.1F, -0.1F);
  fill(half_b, 0.2F, -0.2F);
  fill(double_block, 0.3F, -0.3F);
  assert(adapter.push(half_a));
  assert(!adapter_bus.read(adapted));
  assert(adapter.push(half_b));
  assert(adapter_bus.read(adapted));
  for (std::size_t frame = 0; frame < 64; ++frame) {
    assert(sar::tests::nearly_equal(adapted.channel(0)[frame], 0.1F));
    assert(sar::tests::nearly_equal(adapted.channel(1)[frame], -0.1F));
    assert(sar::tests::nearly_equal(adapted.channel(0)[frame + 64], 0.2F));
    assert(sar::tests::nearly_equal(adapted.channel(1)[frame + 64], -0.2F));
  }
  assert(adapter.push(double_block));
  assert(adapter_bus.read(adapted));
  expect(adapted, 0.3F, -0.3F);
  assert(adapter_bus.read(adapted));
  expect(adapted, 0.3F, -0.3F);

  sar::platform::VirtualAsioRenderBus bus(2, 16, 2, 2);
  assert(bus.channels() == 2);
  assert(bus.frames() == 16);
  assert(bus.accepts_consumer_format(2, 16));
  assert(!bus.accepts_consumer_format(1, 16));
  assert(!bus.accepts_consumer_format(2, 32));

  auto first = bus.attach();
  auto second = bus.attach();
  auto rejected = bus.attach();
  assert(first.valid());
  assert(second.valid());
  assert(!rejected.valid());

  sar::realtime::AudioBuffer first_block(2, 16);
  sar::realtime::AudioBuffer second_block(2, 16);
  sar::realtime::AudioBuffer mixed(2, 16);
  fill(first_block, 0.25F, -0.5F);
  fill(second_block, 0.5F, 0.25F);
  assert(first.push(first_block));
  assert(second.push(second_block));
  assert(bus.read(mixed));
  expect(mixed, 0.75F, -0.25F);

  first_block.channel(0)[0] = std::numeric_limits<float>::infinity();
  assert(first.push(first_block));
  assert(bus.read(mixed));
  assert(mixed.channel(0)[0] == 0.0F);
  first_block.channel(0)[0] = 0.25F;

  mixed.channel(0)[0] = 99.0F;
  assert(!bus.read(mixed));
  expect(mixed, 0.0F, 0.0F);

  assert(first.push(first_block));
  assert(first.push(first_block));
  assert(!first.push(first_block));
  assert(bus.read(mixed));
  expect(mixed, 0.25F, -0.5F);
  assert(bus.read(mixed));
  expect(mixed, 0.25F, -0.5F);

  first.reset();
  auto replacement = bus.attach();
  assert(replacement.valid());
  assert(!first.valid());

  std::atomic_bool producer_done = false;
  std::atomic_bool failed = false;
  std::thread producer([&] {
    for (std::size_t block = 0; block < 10'000; ++block) {
      while (!replacement.push(first_block)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });
  std::thread consumer([&] {
    std::size_t consumed = 0;
    while (consumed < 10'000) {
      if (!bus.read(mixed)) {
        if (producer_done.load(std::memory_order_acquire) &&
            consumed < 10'000) {
          failed.store(true, std::memory_order_release);
          return;
        }
        std::this_thread::yield();
        continue;
      }
      expect(mixed, 0.25F, -0.5F);
      ++consumed;
    }
  });
  producer.join();
  consumer.join();
  assert(!failed.load(std::memory_order_acquire));

  const auto stats = bus.stats();
  assert(stats.pushed_blocks >= 10'005);
  assert(stats.dropped_blocks >= 1);
  assert(stats.consumed_blocks >= 10'005);
  assert(stats.mixed_blocks >= 10'004);
  assert(stats.silent_reads >= 1);
  assert(stats.non_finite_samples == 1);
  assert(stats.maximum_queue_depth == 2);
  assert(stats.active_producers == 2);
  assert(sar::tests::nearly_equal(stats.peak, 0.75F));
  const auto diagnostics = bus.diagnostics();
  assert(diagnostics.pushed_blocks == stats.pushed_blocks);
  assert(diagnostics.producer_underflows == stats.producer_underflows);
  assert(diagnostics.producer_overflows == stats.producer_overflows);
  assert(diagnostics.consumed_blocks == stats.consumed_blocks);
  assert(diagnostics.maximum_queue_depth == stats.maximum_queue_depth);
  assert(sar::tests::nearly_equal(diagnostics.peak, stats.peak));

  // A healthy producer must not hide starvation from a slower clock domain.
  sar::platform::VirtualAsioRenderBus drift_bus(2, 16, 2, 2);
  auto faster_clock = drift_bus.attach();
  auto slower_clock = drift_bus.attach();
  constexpr std::size_t kDriftCycles = 100'000;
  constexpr std::size_t kSlowClockSlipInterval = 1'000;
  for (std::size_t cycle = 0; cycle < kDriftCycles; ++cycle) {
    assert(faster_clock.push(first_block));
    if (cycle % kSlowClockSlipInterval != 0) {
      assert(slower_clock.push(second_block));
    }
    assert(drift_bus.read(mixed));
  }
  const auto drift_stats = drift_bus.stats();
  assert(drift_stats.producer_underflows ==
         kDriftCycles / kSlowClockSlipInterval);
  assert(drift_stats.producer_overflows == 0);
  assert(drift_stats.silent_reads == 0);

  sar::realtime::AudioBuffer wrong_format(1, 16);
  assert(!faster_clock.push(wrong_format));
  assert(drift_bus.stats().producer_overflows == 0);
  assert(faster_clock.push(first_block));
  assert(faster_clock.push(first_block));
  assert(!faster_clock.push(first_block));
  const auto overflow_stats = drift_bus.stats();
  assert(overflow_stats.producer_overflows == 1);
  assert(overflow_stats.dropped_blocks == 2);

  std::cout << "Virtual ASIO render bus smoke test passed\n";
  return 0;
}
