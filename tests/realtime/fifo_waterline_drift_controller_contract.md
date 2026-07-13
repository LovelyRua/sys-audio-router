# FIFO Waterline Drift Controller Smoke Contract

This contract defines the deterministic long-running smoke test to add when the
FIFO waterline drift controller has a public core API. Do not add the CMake
target until the test compiles against that API.

## Required controller surface

The test needs a stateful controller with equivalent semantics to:

```cpp
struct FifoWaterlineControllerConfig {
  double target_fill_frames;
  double proportional_gain;
  double integral_gain;
  double maximum_correction_ppm;
  double maximum_slew_ppm_per_second;
};

struct FifoWaterlineControllerResult {
  double correction_ppm;
  double fill_error_frames;
  bool saturated;
};

class FifoWaterlineDriftController {
 public:
  explicit FifoWaterlineDriftController(FifoWaterlineControllerConfig config);
  FifoWaterlineControllerResult update(double fill_frames,
                                       double elapsed_seconds) noexcept;
  void reset(double fill_frames) noexcept;
};
```

Names may differ. The behavior under test must remain observable without
exposing controller internals. The correction sign convention must be stated by
the production API and applied consistently in the simulation.

## Deterministic plant

Use doubles for the simulated FIFO so sub-frame drift is preserved. Do not use
sleep, threads, random input, WASAPI, or a wall clock.

Constants:

```cpp
constexpr double sample_rate = 48000.0;
constexpr double callback_seconds = 128.0 / sample_rate;
constexpr double fifo_capacity = 2048.0;
constexpr double target_fill = 1024.0;
constexpr double initial_fill = target_fill;
constexpr double maximum_correction_ppm = 300.0;
constexpr std::size_t iterations = 225000;  // 10 virtual minutes
```

For each iteration, call the controller once and advance this plant:

```cpp
const double source_frames =
    128.0 * (1.0 + source_error_ppm / 1'000'000.0);
const double corrected_consumption =
    128.0 * (1.0 + correction_sign * correction_ppm / 1'000'000.0);

fill += source_frames - corrected_consumption;
fill = std::clamp(fill, 0.0, fifo_capacity);
```

Choose `correction_sign` from the production API contract. A positive source
error must eventually produce a correction that increases consumption; a
negative source error must eventually decrease consumption.

Track every correction and fill value. Also count lower/upper FIFO boundary
hits, saturated iterations, saturation sign changes, and correction sign
changes after the settling window.

## Scenarios

Run the same helper for both:

1. `source_error_ppm = +100.0` (capture clock slightly fast).
2. `source_error_ppm = -100.0` (capture clock slightly slow).

Before each scenario, construct or reset a fresh controller at `target_fill`.
The test must not carry integral state between scenarios.

## Assertions

All assertions are deterministic and apply to both scenarios:

1. Every result and simulated fill value is finite.
2. `abs(correction_ppm) <= maximum_correction_ppm + 1e-9` at every iteration.
3. The FIFO never reaches `0` or `fifo_capacity` after the initial sample.
4. During the final 60 virtual seconds, mean fill is within 5% of target:
   `abs(mean_fill - target_fill) <= 0.05 * target_fill`.
5. During the final 60 seconds, peak fill error is below 15% of capacity.
6. During the final 60 seconds, mean correction cancels source drift within
   10 ppm after applying the API sign convention.
7. No iteration is saturated during the final 60 seconds.
8. Saturation occupies less than 5% of the full run.
9. Saturation sign changes at most twice over the full run. Alternating between
   positive and negative limits is an oscillatory failure.
10. During the final 60 seconds, correction crosses its mean at most 12 times.
    This rejects persistent limit cycles while permitting a damped approach.
11. Resetting at target fill returns the next correction to the documented
    neutral/reset value and clears accumulated integral bias.

If controller tuning intentionally changes, adjust gains in production
configuration rather than weakening the FIFO boundary, bounded-output, or
non-oscillation assertions.

## Smoke test shape

The eventual file should be
`tests/realtime/fifo_waterline_drift_controller_smoke.cpp`, use the repository's
small `expect(condition, message)` style, print one compact diagnostic line per
scenario on failure, and print a single pass line on success.

Suggested result aggregate:

```cpp
struct SimulationSummary {
  double final_fill;
  double final_window_mean_fill;
  double final_window_peak_fill_error;
  double final_window_mean_correction_ppm;
  double maximum_abs_correction_ppm;
  std::size_t fifo_boundary_hits;
  std::size_t saturated_iterations;
  std::size_t saturation_sign_changes;
  std::size_t final_window_mean_crossings;
  bool all_finite;
};
```

When the core API exists, register exactly one CTest target named
`fifo_waterline_drift_controller_smoke`. The simulation should finish in well
under one second in release builds and must produce identical results across
repeated runs.
