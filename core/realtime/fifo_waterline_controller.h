#pragma once

#include <cstddef>

namespace sar::realtime {

struct FifoWaterlineControllerConfig {
  std::size_t target_fill_frames = 0;
  double proportional_ppm_per_frame = 0.5;
  double integral_ppm_per_frame_second = 0.05;
  double maximum_correction_ppm = 500.0;
  double maximum_slew_ppm_per_second = 100.0;
};

class FifoWaterlineController {
 public:
  explicit FifoWaterlineController(FifoWaterlineControllerConfig config) noexcept;

  // Positive correction means the consumer must run faster because fill is
  // above target. An output/input SRC ratio therefore divides by 1 + ppm.
  [[nodiscard]] double update(double fill_frames,
                              double elapsed_seconds) noexcept;
  void reset() noexcept;
  [[nodiscard]] double correction_ppm() const noexcept;
  [[nodiscard]] double integral_ppm() const noexcept;
  [[nodiscard]] const FifoWaterlineControllerConfig& config() const noexcept;

 private:
  FifoWaterlineControllerConfig config_;
  double integral_ppm_ = 0.0;
  double correction_ppm_ = 0.0;
};

}  // namespace sar::realtime
