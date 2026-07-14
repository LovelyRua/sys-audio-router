#include "core/platform/wasapi_realtime_error.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <thread>
#include <type_traits>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool same_error(const sar::platform::WasapiRealtimeErrorRecord& lhs,
                const sar::platform::WasapiRealtimeErrorRecord& rhs) {
  return lhs.code == rhs.code && lhs.context == rhs.context && lhs.value == rhs.value;
}

}  // namespace

int main() {
  using sar::platform::WasapiRealtimeErrorBatch;
  using sar::platform::WasapiRealtimeErrorChannel;
  using sar::platform::WasapiRealtimeErrorRecord;
  using sar::platform::kWasapiRealtimeErrorCapacity;
  using sar::platform::kWasapiRealtimeErrorOverflowCode;

  static_assert(std::is_trivially_copyable_v<WasapiRealtimeErrorRecord>);
  static_assert(std::is_trivially_copyable_v<WasapiRealtimeErrorBatch>);
  static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  constexpr WasapiRealtimeErrorRecord original{0x1234U, 0xABCDU, 0x89ABCDEFU};
  const auto unpacked = sar::platform::unpack_wasapi_realtime_error(
      sar::platform::pack_wasapi_realtime_error(original));
  if (const auto failure = expect(same_error(original, unpacked),
                                  "Expected error record pack/unpack round trip")) {
    return failure;
  }

  WasapiRealtimeErrorBatch overflow{};
  for (std::uint32_t index = 0; index < 12; ++index) {
    (void)overflow.push({static_cast<std::uint16_t>(index + 1),
                         static_cast<std::uint16_t>(index), 1000U + index});
  }
  if (const auto failure = expect(
          overflow.size() == kWasapiRealtimeErrorCapacity && overflow.overflowed() &&
              overflow.overflow_count == 5 &&
              overflow.records[6].code == 7 &&
              overflow.records[7].code == kWasapiRealtimeErrorOverflowCode &&
              overflow.records[7].value == overflow.overflow_count,
          "Expected deterministic overflow marker and count")) {
    return failure;
  }

  WasapiRealtimeErrorChannel channel;
  channel.publish(overflow);
  const auto published = channel.snapshot();
  if (const auto failure = expect(
          published.count == overflow.count &&
              published.overflow_count == overflow.overflow_count &&
              same_error(published.records.back(), overflow.records.back()),
          "Expected published overflow batch snapshot")) {
    return failure;
  }
  channel.clear();
  if (const auto failure = expect(channel.snapshot().empty(),
                                  "Expected clear to publish an empty batch")) {
    return failure;
  }

  constexpr std::uint32_t kPublicationCount = 100000;
  std::atomic_bool start = false;
  std::atomic_bool done = false;
  std::atomic_bool failed = false;

  std::thread publisher([&] {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    for (std::uint32_t generation = 1; generation <= kPublicationCount; ++generation) {
      WasapiRealtimeErrorBatch batch{};
      for (std::uint16_t index = 0; index < kWasapiRealtimeErrorCapacity; ++index) {
        (void)batch.push({static_cast<std::uint16_t>(index + 1), index, generation});
      }
      channel.publish(batch);
    }
    done.store(true, std::memory_order_release);
  });

  start.store(true, std::memory_order_release);
  while (!done.load(std::memory_order_acquire)) {
    const auto batch = channel.snapshot();
    if (batch.empty()) {
      continue;
    }
    if (batch.count != kWasapiRealtimeErrorCapacity || batch.overflowed()) {
      failed.store(true, std::memory_order_release);
      break;
    }
    const auto generation = batch.records.front().value;
    for (std::uint16_t index = 0; index < kWasapiRealtimeErrorCapacity; ++index) {
      const auto& error = batch.records[index];
      if (error.code != index + 1 || error.context != index || error.value != generation) {
        failed.store(true, std::memory_order_release);
        break;
      }
    }
  }
  publisher.join();

  const auto final_batch = channel.snapshot();
  if (const auto failure = expect(
          !failed.load(std::memory_order_acquire) &&
              final_batch.records.front().value == kPublicationCount,
          "Expected coherent concurrent snapshots")) {
    return failure;
  }

  overflow.clear();
  if (const auto failure = expect(overflow.empty() && !overflow.overflowed(),
                                  "Expected batch clear to reset all metadata")) {
    return failure;
  }

  std::cout << "WASAPI realtime error smoke test passed\n";
  return 0;
}
