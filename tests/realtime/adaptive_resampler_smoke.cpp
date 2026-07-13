#include "core/realtime/adaptive_resampler.h"

#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

int main() {
  using sar::realtime::AdaptiveResampler;
  using sar::realtime::AdaptiveResamplerQuality;
  using sar::realtime::AdaptiveResamplerStatus;

  AdaptiveResampler resampler;
  assert(!resampler.initialized());
  assert(resampler.initialize(0) == AdaptiveResamplerStatus::invalid_argument);
  assert(resampler.initialize(2, AdaptiveResamplerQuality::fastest) ==
         AdaptiveResamplerStatus::success);
  assert(resampler.initialized());
  assert(resampler.channels() == 2);

  constexpr std::uint32_t input_frames = 1024;
  constexpr std::uint32_t output_frames = 2048;
  std::vector<float> input(input_frames * 2);
  std::vector<float> output(output_frames * 2);
  for (std::uint32_t frame = 0; frame < input_frames; ++frame) {
    const float sample = std::sin(static_cast<float>(frame) * 0.03F);
    input[frame * 2] = sample;
    input[frame * 2 + 1] = -sample;
  }

  const auto first = resampler.process(input, input_frames, output, output_frames, 1.0);
  assert(first.ok());
  assert(first.input_frames_used > 0);
  assert(first.input_frames_used <= input_frames);
  assert(first.output_frames_generated > 0);
  assert(first.output_frames_generated <= output_frames);

  resampler.reset();
  const auto adjusted =
      resampler.process(input, input_frames, output, output_frames, 1.0002);
  assert(adjusted.ok());
  assert(adjusted.input_frames_used > 0);
  assert(adjusted.output_frames_generated > 0);

  const auto invalid_ratio =
      resampler.process(input, input_frames, output, output_frames, 0.0);
  assert(invalid_ratio.status == AdaptiveResamplerStatus::invalid_argument);

  AdaptiveResampler moved = std::move(resampler);
  assert(moved.initialized());
  assert(!resampler.initialized());
  moved.release();
  assert(!moved.initialized());
}
