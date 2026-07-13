#include "core/realtime/adaptive_resampler.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

struct BlockRecord {
  double ratio = 1.0;
  std::uint32_t input_frames_used = 0;
  std::uint32_t output_frames_generated = 0;
};

int fail(std::size_t block, const char* message) {
  std::cerr << "Block " << block << ": " << message << '\n';
  return 1;
}

}  // namespace

int main() {
  using sar::realtime::AdaptiveResampler;
  using sar::realtime::AdaptiveResamplerQuality;
  using sar::realtime::AdaptiveResamplerStatus;

  constexpr std::size_t channels = 2;
  constexpr std::uint32_t input_frames_per_block = 256;
  constexpr std::uint32_t output_frame_capacity = 320;
  constexpr std::size_t block_count = 32;
  constexpr std::array ratios{
      1.0, 1.0002, 0.9998, 1.0003, 0.9997, 1.0001, 0.9999};

  AdaptiveResampler resampler;
  if (resampler.initialize(channels, AdaptiveResamplerQuality::fastest) !=
      AdaptiveResamplerStatus::success) {
    std::cerr << "Failed to initialize adaptive resampler\n";
    return 1;
  }

  std::vector<float> input(input_frames_per_block * channels);
  std::vector<float> output(output_frame_capacity * channels);
  std::vector<BlockRecord> records;
  records.reserve(block_count);

  std::uint64_t total_input_frames_used = 0;
  std::uint64_t total_output_frames_generated = 0;
  bool generated_finite_nonzero_output = false;
  std::uint64_t source_frame = 0;

  for (std::size_t block = 0; block < block_count; ++block) {
    for (std::uint32_t frame = 0; frame < input_frames_per_block; ++frame) {
      const auto phase = static_cast<double>(source_frame + frame) * 0.017;
      const auto sample = static_cast<float>(std::sin(phase));
      input[frame * channels] = sample;
      input[frame * channels + 1] = -sample;
    }
    source_frame += input_frames_per_block;

    const double ratio = ratios[block % ratios.size()];
    const auto result = resampler.process(
        input, input_frames_per_block, output, output_frame_capacity, ratio);
    records.push_back({ratio,
                       result.input_frames_used,
                       result.output_frames_generated});

    if (!result.ok() || result.backend_error != 0) {
      return fail(block, "resampling failed");
    }
    if (result.input_frames_used == 0 ||
        result.input_frames_used > input_frames_per_block) {
      return fail(block, "input frame consumption was outside block bounds");
    }
    if (result.output_frames_generated == 0 ||
        result.output_frames_generated > output_frame_capacity) {
      return fail(block, "output frame count was outside block bounds");
    }

    const auto generated_samples =
        static_cast<std::size_t>(result.output_frames_generated) * channels;
    for (std::size_t sample = 0; sample < generated_samples; ++sample) {
      if (!std::isfinite(output[sample])) {
        return fail(block, "generated output contained a non-finite sample");
      }
      generated_finite_nonzero_output =
          generated_finite_nonzero_output || output[sample] != 0.0F;
    }

    total_input_frames_used += result.input_frames_used;
    total_output_frames_generated += result.output_frames_generated;
  }

  if (records.size() != block_count) {
    std::cerr << "Expected one streaming result record per input block\n";
    return 1;
  }
  if (total_input_frames_used != block_count * input_frames_per_block) {
    std::cerr << "Streaming calls did not consume every submitted input frame\n";
    return 1;
  }
  if (!generated_finite_nonzero_output || total_output_frames_generated == 0) {
    std::cerr << "Streaming calls did not eventually generate finite audio\n";
    return 1;
  }
  std::cout << "Adaptive resampler streaming smoke test passed: "
            << total_input_frames_used << " input frames, "
            << total_output_frames_generated << " output frames\n";
  return 0;
}
