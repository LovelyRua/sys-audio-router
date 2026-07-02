#pragma once

#include <cmath>
#include <cstddef>
#include <iostream>

namespace sar::tests {

constexpr float kFloatEpsilon = 0.000001F;

inline bool nearly_equal(float left, float right) {
  return std::fabs(left - right) <= kFloatEpsilon;
}

inline int fail_sample(const char* message, std::size_t channel, std::size_t frame) {
  std::cerr << message << " at channel " << channel << ", frame " << frame << '\n';
  return 1;
}

}  // namespace sar::tests

