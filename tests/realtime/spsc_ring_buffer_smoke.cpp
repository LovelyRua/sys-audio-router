#include "core/realtime/spsc_ring_buffer.h"

#include <iostream>

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
    const auto value = queue.pop();
    if (!value.has_value() || *value != expected) {
      std::cerr << "Unexpected pop value\n";
      return 1;
    }
  }

  if (queue.pop().has_value()) {
    std::cerr << "Queue should be empty after popping all values\n";
    return 1;
  }

  if (!queue.push(5)) {
    std::cerr << "Queue should accept value after wraparound\n";
    return 1;
  }

  const auto wrapped = queue.pop();
  if (!wrapped.has_value() || *wrapped != 5) {
    std::cerr << "Unexpected wraparound value\n";
    return 1;
  }

  std::cout << "SPSC ring buffer smoke test passed\n";
  return 0;
}

