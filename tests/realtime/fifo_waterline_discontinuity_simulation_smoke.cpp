#include "core/realtime/fifo_waterline_controller.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

namespace {

constexpr double kFramesPerCycle = 128.0;
constexpr double kSampleRate = 48000.0;
constexpr double kElapsedSeconds = kFramesPerCycle / kSampleRate;
constexpr double kProducerDriftPpm = -300.0;
constexpr double kFifoCapacityFrames = 2048.0;
constexpr double kTargetFillFrames = 1024.0;
constexpr double kUnderflowProxyFrames = kTargetFillFrames - 128.0;
constexpr std::size_t kFirstDiscontinuityCycle = 225000;  // 10 virtual minutes.
constexpr std::size_t kDiscontinuityPeriodCycles = 112500;  // 5 virtual minutes.
constexpr std::size_t kSimulationCycles = 900000;  // 40 virtual minutes.
constexpr std::size_t kRecoveryStableCycles = 128;
constexpr double kRecoveryCorrectionTolerancePpm = 10.0;
constexpr double kRecoveryFillToleranceFrames = 32.0;

enum class DiscontinuityStrategy {
  ResetController,
  PreserveLearnedCorrection,
};

struct EventSummary {
  double minimum_fill_frames = kTargetFillFrames;
  double maximum_fill_frames = kTargetFillFrames;
  std::size_t underflow_proxy_cycles = 0;
  std::size_t recovery_cycles = std::numeric_limits<std::size_t>::max();
};

struct SimulationSummary {
  double minimum_post_discontinuity_fill_frames = kFifoCapacityFrames;
  double maximum_post_discontinuity_fill_frames = 0.0;
  double final_correction_ppm = 0.0;
  std::size_t discontinuities = 0;
  std::size_t fifo_boundary_hits = 0;
  std::size_t underflow_proxy_cycles = 0;
  std::size_t minimum_recovery_cycles = std::numeric_limits<std::size_t>::max();
  std::size_t maximum_recovery_cycles = 0;
  bool all_finite = true;
};

SimulationSummary simulate(DiscontinuityStrategy strategy) {
  sar::realtime::FifoWaterlineController controller({
      .target_fill_frames = static_cast<std::size_t>(kTargetFillFrames),
      .proportional_ppm_per_frame = 0.5,
      .integral_ppm_per_frame_second = 0.05,
      .maximum_correction_ppm = 500.0,
      .maximum_slew_ppm_per_second = 100.0,
  });

  SimulationSummary summary;
  EventSummary event;
  double fill_frames = kTargetFillFrames;
  std::size_t event_age_cycles = 0;
  std::size_t stable_recovery_cycles = 0;
  bool event_active = false;

  const auto finish_event = [&] {
    assert(event_active);
    assert(event.recovery_cycles != std::numeric_limits<std::size_t>::max());
    summary.minimum_post_discontinuity_fill_frames = std::min(
        summary.minimum_post_discontinuity_fill_frames,
        event.minimum_fill_frames);
    summary.maximum_post_discontinuity_fill_frames = std::max(
        summary.maximum_post_discontinuity_fill_frames,
        event.maximum_fill_frames);
    summary.underflow_proxy_cycles += event.underflow_proxy_cycles;
    summary.minimum_recovery_cycles =
        std::min(summary.minimum_recovery_cycles, event.recovery_cycles);
    summary.maximum_recovery_cycles =
        std::max(summary.maximum_recovery_cycles, event.recovery_cycles);
  };

  for (std::size_t cycle = 0; cycle < kSimulationCycles; ++cycle) {
    const bool discontinuity = cycle >= kFirstDiscontinuityCycle &&
                               (cycle - kFirstDiscontinuityCycle) %
                                       kDiscontinuityPeriodCycles ==
                                   0;
    if (discontinuity) {
      if (event_active) {
        finish_event();
      }
      ++summary.discontinuities;
      fill_frames = kTargetFillFrames;  // Both strategies re-prime the FIFO.
      if (strategy == DiscontinuityStrategy::ResetController) {
        controller.reset();
      }
      event = {};
      event_age_cycles = 0;
      stable_recovery_cycles = 0;
      event_active = true;
    }

    const auto correction_ppm = controller.update(fill_frames, kElapsedSeconds);
    const auto produced_frames =
        kFramesPerCycle * (1.0 + kProducerDriftPpm / 1'000'000.0);
    const auto consumed_frames =
        kFramesPerCycle * (1.0 + correction_ppm / 1'000'000.0);
    fill_frames += produced_frames - consumed_frames;

    summary.all_finite = summary.all_finite && std::isfinite(correction_ppm) &&
                         std::isfinite(fill_frames);
    if (fill_frames <= 0.0 || fill_frames >= kFifoCapacityFrames) {
      ++summary.fifo_boundary_hits;
    }
    fill_frames = std::clamp(fill_frames, 0.0, kFifoCapacityFrames);

    if (!event_active) {
      continue;
    }

    ++event_age_cycles;
    event.minimum_fill_frames = std::min(event.minimum_fill_frames, fill_frames);
    event.maximum_fill_frames = std::max(event.maximum_fill_frames, fill_frames);
    if (fill_frames < kUnderflowProxyFrames) {
      ++event.underflow_proxy_cycles;
    }

    const bool recovered =
        std::fabs(correction_ppm - kProducerDriftPpm) <=
            kRecoveryCorrectionTolerancePpm &&
        std::fabs(fill_frames - kTargetFillFrames) <=
            kRecoveryFillToleranceFrames;
    stable_recovery_cycles = recovered ? stable_recovery_cycles + 1 : 0;
    if (event.recovery_cycles == std::numeric_limits<std::size_t>::max() &&
        stable_recovery_cycles >= kRecoveryStableCycles) {
      event.recovery_cycles = event_age_cycles - kRecoveryStableCycles;
    }
  }

  finish_event();
  summary.final_correction_ppm = controller.correction_ppm();
  return summary;
}

}  // namespace

int main() {
  const auto reset = simulate(DiscontinuityStrategy::ResetController);
  const auto preserve =
      simulate(DiscontinuityStrategy::PreserveLearnedCorrection);

  assert(reset.all_finite);
  assert(preserve.all_finite);
  assert(reset.discontinuities == 6);
  assert(preserve.discontinuities == reset.discontinuities);
  assert(reset.fifo_boundary_hits == 0);
  assert(preserve.fifo_boundary_hits == 0);
  assert(std::fabs(reset.final_correction_ppm - kProducerDriftPpm) < 10.0);
  assert(std::fabs(preserve.final_correction_ppm - kProducerDriftPpm) < 1.0);

  // Retaining the learned drift keeps every re-prime near target, while a full
  // reset repeatedly recreates the same low-water excursion and relearning cost.
  assert(reset.minimum_post_discontinuity_fill_frames <
         kUnderflowProxyFrames);
  assert(preserve.minimum_post_discontinuity_fill_frames >
         kTargetFillFrames - 1.0);
  assert(preserve.maximum_post_discontinuity_fill_frames <
         kTargetFillFrames + 1.0);
  assert(reset.underflow_proxy_cycles > 10000);
  assert(preserve.underflow_proxy_cycles == 0);
  assert(reset.minimum_recovery_cycles > 75000);
  assert(reset.maximum_recovery_cycles < kDiscontinuityPeriodCycles);
  assert(preserve.maximum_recovery_cycles <= 1);
  assert(reset.minimum_recovery_cycles >
         preserve.maximum_recovery_cycles + 75000);

  std::cout << "fifo discontinuity simulation passed: reset[min="
            << reset.minimum_post_discontinuity_fill_frames
            << ", recovery_cycles=" << reset.maximum_recovery_cycles
            << ", underflow_proxy=" << reset.underflow_proxy_cycles
            << "] preserve[min="
            << preserve.minimum_post_discontinuity_fill_frames
            << ", recovery_cycles=" << preserve.maximum_recovery_cycles
            << ", underflow_proxy=" << preserve.underflow_proxy_cycles << "]\n";
  return 0;
}
