#include "core/realtime/fifo_waterline_controller.h"

#include <algorithm>
#include <cmath>

namespace sar::realtime {

namespace {
double finite_nonnegative(double value) noexcept {
  return std::isfinite(value) && value >= 0.0 ? value : 0.0;
}
}  // namespace

FifoWaterlineController::FifoWaterlineController(
    FifoWaterlineControllerConfig config) noexcept
    : config_(config) {
  config_.proportional_ppm_per_frame =
      finite_nonnegative(config_.proportional_ppm_per_frame);
  config_.integral_ppm_per_frame_second =
      finite_nonnegative(config_.integral_ppm_per_frame_second);
  config_.maximum_correction_ppm =
      finite_nonnegative(config_.maximum_correction_ppm);
  config_.maximum_slew_ppm_per_second =
      finite_nonnegative(config_.maximum_slew_ppm_per_second);
}

double FifoWaterlineController::update(double fill_frames,
                                       double elapsed_seconds) noexcept {
  if (!std::isfinite(fill_frames) || !std::isfinite(elapsed_seconds) ||
      elapsed_seconds <= 0.0) {
    return correction_ppm_;
  }
  const auto error_frames = fill_frames -
                            static_cast<double>(config_.target_fill_frames);
  const auto limit = config_.maximum_correction_ppm;
  integral_ppm_ = std::clamp(
      integral_ppm_ + error_frames * config_.integral_ppm_per_frame_second *
                          elapsed_seconds,
      -limit,
      limit);
  const auto desired = std::clamp(
      error_frames * config_.proportional_ppm_per_frame + integral_ppm_,
      -limit,
      limit);
  const auto maximum_step =
      config_.maximum_slew_ppm_per_second * elapsed_seconds;
  const auto delta =
      std::clamp(desired - correction_ppm_, -maximum_step, maximum_step);
  correction_ppm_ = std::clamp(correction_ppm_ + delta, -limit, limit);
  return correction_ppm_;
}

void FifoWaterlineController::reset() noexcept {
  integral_ppm_ = 0.0;
  correction_ppm_ = 0.0;
}

double FifoWaterlineController::correction_ppm() const noexcept {
  return correction_ppm_;
}

double FifoWaterlineController::integral_ppm() const noexcept {
  return integral_ppm_;
}

const FifoWaterlineControllerConfig& FifoWaterlineController::config() const noexcept {
  return config_;
}

}  // namespace sar::realtime
