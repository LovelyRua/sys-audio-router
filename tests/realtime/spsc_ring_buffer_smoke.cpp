#include "core/realtime/spsc_ring_buffer.h"

#include <iostream>
#include <limits>

int main() {
  sar::realtime::SpscRingBuffer<int> queue(3);

  if (!queue.empty()) {
    std::cerr << "Queue should start empty\n";
    return 1;
  }

  if (!queue.push(1) || !queue.push(2) || !queue.push(3)) {
    std::cerr << "Queue should accept three values\n";
    return 1;
  }

  if (!queue.full()) {
    std::cerr << "Queue should be full\n";
    return 1;
  }

  if (queue.push(4)) {
    std::cerr << "Queue should reject push when full\n";
    return 1;
  }

  for (int expected = 1; expected <= 3; ++expected) {
    int value = 0;
    if (!queue.try_pop(value) || value != expected) {
      std::cerr << "Unexpected pop value\n";
      return 1;
    }
  }

  int value = 99;
  if (queue.try_pop(value) || value != 99) {
    std::cerr << "Queue should be empty after popping all values\n";
    return 1;
  }

  if (!queue.push(5)) {
    std::cerr << "Queue should accept value after wraparound\n";
    return 1;
  }

  if (!queue.try_pop(value) || value != 5) {
    std::cerr << "Unexpected wraparound value\n";
    return 1;
  }

  try {
    sar::realtime::SpscRingBuffer<int> invalid(
        std::numeric_limits<std::size_t>::max());
    (void)invalid;
    std::cerr << "Queue should reject an overflowing capacity\n";
    return 1;
  } catch (const std::invalid_argument&) {
  }

  std::cout << "SPSC ring buffer smoke test passed\n";
  return 0;
}
