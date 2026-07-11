#include "core/realtime/atomic_float_parameter.h"
#include "tests/realtime/test_helpers.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <thread>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  sar::realtime::AtomicFloatParameter parameter(0.25F);
  if (const auto failure = expect(sar::tests::nearly_equal(parameter.get(), 0.25F),
                                  "Expected finite initial parameter")) {
    return failure;
  }
  if (const auto failure = expect(parameter.set(-0.5F),
                                  "Expected finite parameter update")) {
    return failure;
  }
  if (const auto failure = expect(!parameter.set(std::numeric_limits<float>::quiet_NaN()) &&
                                      !parameter.set(std::numeric_limits<float>::infinity()) &&
                                      sar::tests::nearly_equal(parameter.get(), -0.5F),
                                  "Expected rejected non-finite values to preserve parameter")) {
    return failure;
  }

  std::atomic_bool start = false;
  std::atomic_bool failed = false;
  std::thread writer([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    for (int index = 0; index < 20000; ++index) {
      if (!parameter.set(static_cast<float>(index % 9) * 0.125F)) {
        failed.store(true, std::memory_order_release);
      }
    }
  });

  start.store(true, std::memory_order_release);
  for (int index = 0; index < 20000; ++index) {
    if (!std::isfinite(parameter.get())) {
      failed.store(true, std::memory_order_release);
      break;
    }
  }
  writer.join();

  if (const auto failure = expect(!failed.load(std::memory_order_acquire),
                                  "Expected concurrent parameter reads to remain finite")) {
    return failure;
  }

  std::cout << "Atomic float parameter smoke test passed\n";
  return 0;
}
