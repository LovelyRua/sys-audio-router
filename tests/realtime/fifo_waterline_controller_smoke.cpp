#include "core/realtime/fifo_waterline_controller.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
double simulate(double producer_error_ppm) {
  constexpr double frames_per_cycle = 128.0;
  constexpr double elapsed_seconds = frames_per_cycle / 48000.0;
  constexpr std::size_t target_frames = 2048;
  sar::realtime::FifoWaterlineController controller({
      .target_fill_frames = target_frames,
      .proportional_ppm_per_frame = 0.5,
      .integral_ppm_per_frame_second = 0.05,
      .maximum_correction_ppm = 500.0,
      .maximum_slew_ppm_per_second = 100.0,
  });
  double fill = static_cast<double>(target_frames);
  double minimum_fill = fill;
  double maximum_fill = fill;
  for (std::size_t cycle = 0; cycle < 200000; ++cycle) {
    const auto correction = controller.update(fill, elapsed_seconds);
    fill += frames_per_cycle * producer_error_ppm * 0.000001;
    fill -= frames_per_cycle * correction * 0.000001;
    minimum_fill = std::min(minimum_fill, fill);
    maximum_fill = std::max(maximum_fill, fill);
  }
  assert(minimum_fill > 512.0);
  assert(maximum_fill < 3584.0);
  assert(std::fabs(fill - static_cast<double>(target_frames)) < 32.0);
  assert(std::fabs(controller.correction_ppm() - producer_error_ppm) < 5.0);
  return controller.correction_ppm();
}
}  // namespace

int main() {
  assert(simulate(100.0) > 0.0);
  assert(simulate(-100.0) < 0.0);
  sar::realtime::FifoWaterlineController controller({
      .target_fill_frames = 100,
      .maximum_correction_ppm = 20.0,
      .maximum_slew_ppm_per_second = 10.0,
  });
  assert(controller.update(1000.0, 0.1) == 1.0);
  assert(controller.update(1000.0, 0.1) == 2.0);
  controller.reset();
  assert(controller.correction_ppm() == 0.0);
  assert(controller.integral_ppm() == 0.0);
  return 0;
}
