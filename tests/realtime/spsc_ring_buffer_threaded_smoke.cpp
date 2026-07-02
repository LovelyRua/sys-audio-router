#include "core/realtime/spsc_ring_buffer.h"

#include <atomic>
#include <iostream>
#include <thread>

int main() {
  constexpr int kItemCount = 10000;

  sar::realtime::SpscRingBuffer<int> queue(128);
  std::atomic<bool> producer_done = false;
  std::atomic<bool> failed = false;

  std::thread producer([&queue, &producer_done]() {
    for (int value = 0; value < kItemCount; ++value) {
      while (!queue.push(value)) {
        std::this_thread::yield();
      }
    }
    producer_done.store(true, std::memory_order_release);
  });

  std::thread consumer([&queue, &producer_done, &failed]() {
    int expected = 0;
    while (expected < kItemCount) {
      const auto value = queue.pop();
      if (!value.has_value()) {
        if (producer_done.load(std::memory_order_acquire)) {
          failed.store(true, std::memory_order_release);
          return;
        }
        std::this_thread::yield();
        continue;
      }

      if (*value != expected) {
        failed.store(true, std::memory_order_release);
        return;
      }
      ++expected;
    }
  });

  producer.join();
  consumer.join();

  if (failed.load(std::memory_order_acquire)) {
    std::cerr << "SPSC threaded smoke test observed out-of-order data\n";
    return 1;
  }

  std::cout << "SPSC threaded smoke test passed\n";
  return 0;
}
