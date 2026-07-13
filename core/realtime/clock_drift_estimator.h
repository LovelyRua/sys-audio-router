#pragma once

#include "core/realtime/audio_block_timeline.h"

#include <cmath>
#include <cstdint>

namespace sar::realtime {

struct ClockTimelineSample {
  ClockDomain clock_domain;
  std::uint64_t frame_position = 0;
  std::uint64_t qpc_100ns = 0;
};

struct ClockDriftEstimate {
  bool valid = false;
  double observed_sample_rate = 0.0;
  double nominal_error_ppm = 0.0;
};

struct ClockRateFeedForward {
  bool valid = false;
  double correction_ppm = 0.0;
};

class ClockDriftEstimator {
 public:
  [[nodiscard]] static ClockDriftEstimate estimate(
      const ClockTimelineSample& first,
      const ClockTimelineSample& second) noexcept {
    if (!first.clock_domain.valid() ||
        first.clock_domain != second.clock_domain ||
        second.frame_position <= first.frame_position ||
        second.qpc_100ns <= first.qpc_100ns) {
      return {};
    }

    const auto frame_delta = second.frame_position - first.frame_position;
    const auto time_delta_100ns = second.qpc_100ns - first.qpc_100ns;
    const double nominal_rate = first.clock_domain.nominal_sample_rate;
    const double observed_rate =
        static_cast<double>(frame_delta) * 10'000'000.0 /
        static_cast<double>(time_delta_100ns);
    const double error_ppm =
        (observed_rate / nominal_rate - 1.0) * 1'000'000.0;

    if (!std::isfinite(observed_rate) || observed_rate <= 0.0 ||
        !std::isfinite(error_ppm)) {
      return {};
    }

    return {true, observed_rate, error_ppm};
  }

  [[nodiscard]] static ClockRateFeedForward relative_rate_correction(
      const ClockDriftEstimate& capture,
      const ClockDriftEstimate& render) noexcept {
    if (!capture.valid || !render.valid ||
        !std::isfinite(capture.observed_sample_rate) ||
        !std::isfinite(render.observed_sample_rate) ||
        capture.observed_sample_rate <= 0.0 ||
        render.observed_sample_rate <= 0.0) {
      return {};
    }

    const auto correction_ppm =
        (capture.observed_sample_rate / render.observed_sample_rate - 1.0) *
        1'000'000.0;
    return std::isfinite(correction_ppm)
               ? ClockRateFeedForward{true, correction_ppm}
               : ClockRateFeedForward{};
  }
};

}  // namespace sar::realtime
