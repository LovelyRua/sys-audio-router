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
  using sar::realtime::ClockRateFeedForward;
  using sar::realtime::ClockTimelineSample;

  static_assert(std::is_trivially_copyable_v<ClockTimelineSample>);
  static_assert(std::is_trivially_copyable_v<ClockDriftEstimate>);
  static_assert(std::is_trivially_copyable_v<ClockRateFeedForward>);
  static_assert(noexcept(ClockDriftEstimator::estimate({}, {})));
  static_assert(noexcept(ClockDriftEstimator::relative_rate_correction({}, {})));

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

  const auto relative_nominal = ClockDriftEstimator::relative_rate_correction(
      nominal, nominal);
  assert(relative_nominal.valid);
  assert(near(relative_nominal.correction_ppm, 0.0, 1.0e-9));

  const auto capture_slow = ClockDriftEstimator::relative_rate_correction(
      slow, nominal);
  assert(capture_slow.valid);
  assert(near(capture_slow.correction_ppm, -1000.0, 1.0e-6));

  const auto shared_offset = ClockDriftEstimator::relative_rate_correction(
      fast, fast);
  assert(shared_offset.valid);
  assert(near(shared_offset.correction_ppm, 0.0, 1.0e-9));

  constexpr ClockDomain capture_clock{2, 44100};
  constexpr ClockDomain render_clock{3, 48000};
  constexpr std::uint64_t hundred_seconds_100ns = 1'000'000'000;
  const auto capture_44100_nominal = ClockDriftEstimator::estimate(
      {capture_clock, 0, 10'000'000},
      {capture_clock, 4'410'000, 10'000'000 + hundred_seconds_100ns});
  const auto render_48000_nominal = ClockDriftEstimator::estimate(
      {render_clock, 0, 10'000'000},
      {render_clock, 4'800'000, 10'000'000 + hundred_seconds_100ns});
  const auto mismatched_nominal_rates =
      ClockDriftEstimator::relative_rate_correction(capture_44100_nominal,
                                                    render_48000_nominal);
  assert(mismatched_nominal_rates.valid);
  assert(near(mismatched_nominal_rates.correction_ppm, 0.0, 1.0e-9));

  const auto capture_44100_fast = ClockDriftEstimator::estimate(
      {capture_clock, 0, 10'000'000},
      {capture_clock, 4'410'441, 10'000'000 + hundred_seconds_100ns});
  const auto mismatched_rates_with_drift =
      ClockDriftEstimator::relative_rate_correction(capture_44100_fast,
                                                    render_48000_nominal);
  assert(mismatched_rates_with_drift.valid);
  assert(near(mismatched_rates_with_drift.correction_ppm, 100.0, 1.0e-6));

  assert(!ClockDriftEstimator::relative_rate_correction({}, nominal).valid);

  std::cout << "Clock drift estimator smoke test passed\n";
  return 0;
}
