#include "core/realtime/clock_drift_estimator.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

bool near(double actual, double expected, double tolerance) {
  return std::abs(actual - expected) <= tolerance;
}

}  // namespace

int main() {
  using sar::realtime::ClockDomain;
  using sar::realtime::ClockDriftEstimate;
  using sar::realtime::ClockDriftEstimator;
  using sar::realtime::ClockTimelineSample;

  static_assert(std::is_trivially_copyable_v<ClockTimelineSample>);
  static_assert(std::is_trivially_copyable_v<ClockDriftEstimate>);
  static_assert(noexcept(ClockDriftEstimator::estimate({}, {})));

  constexpr ClockDomain clock{1, 48000};
  const ClockTimelineSample first{clock, 1'000, 20'000'000};

  const auto nominal = ClockDriftEstimator::estimate(
      first, {clock, 49'000, 30'000'000});
  assert(nominal.valid);
  assert(near(nominal.observed_sample_rate, 48000.0, 1.0e-9));
  assert(near(nominal.nominal_error_ppm, 0.0, 1.0e-9));

  const auto fast = ClockDriftEstimator::estimate(
      first, {clock, 49'048, 30'000'000});
  assert(fast.valid);
  assert(near(fast.observed_sample_rate, 48048.0, 1.0e-9));
  assert(near(fast.nominal_error_ppm, 1000.0, 1.0e-6));

  const auto slow = ClockDriftEstimator::estimate(
      first, {clock, 48'952, 30'000'000});
  assert(slow.valid);
  assert(near(slow.observed_sample_rate, 47952.0, 1.0e-9));
  assert(near(slow.nominal_error_ppm, -1000.0, 1.0e-6));

  assert(!ClockDriftEstimator::estimate(first, {clock, 49'000, first.qpc_100ns})
              .valid);
  assert(!ClockDriftEstimator::estimate(first, {clock, first.frame_position,
                                                 30'000'000})
              .valid);
  assert(!ClockDriftEstimator::estimate(first, {clock, 999, 30'000'000}).valid);
  assert(!ClockDriftEstimator::estimate(first, {clock, 49'000, 19'999'999}).valid);
  assert(!ClockDriftEstimator::estimate(
              {clock, std::numeric_limits<std::uint64_t>::max() - 1, 1},
              {clock, 2, 2})
              .valid);
  assert(!ClockDriftEstimator::estimate(
              {clock, 1, std::numeric_limits<std::uint64_t>::max() - 1},
              {clock, 2, 2})
              .valid);
  assert(!ClockDriftEstimator::estimate(first, {{2, 48000}, 49'000, 30'000'000})
              .valid);
  assert(!ClockDriftEstimator::estimate(first, {{1, 0}, 49'000, 30'000'000})
              .valid);

  std::cout << "Clock drift estimator smoke test passed\n";
  return 0;
}
