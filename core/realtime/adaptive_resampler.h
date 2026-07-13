#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace sar::realtime {

enum class AdaptiveResamplerQuality {
  fastest,
  medium,
};

enum class AdaptiveResamplerStatus {
  success,
  not_initialized,
  invalid_argument,
  backend_error,
};

struct AdaptiveResamplerProcessResult {
  AdaptiveResamplerStatus status = AdaptiveResamplerStatus::not_initialized;
  std::uint32_t input_frames_used = 0;
  std::uint32_t output_frames_generated = 0;
  int backend_error = 0;

  [[nodiscard]] bool ok() const noexcept {
    return status == AdaptiveResamplerStatus::success;
  }
};

class AdaptiveResampler {
 public:
  AdaptiveResampler() = default;
  AdaptiveResampler(const AdaptiveResampler&) = delete;
  AdaptiveResampler& operator=(const AdaptiveResampler&) = delete;
  AdaptiveResampler(AdaptiveResampler&& other) noexcept;
  AdaptiveResampler& operator=(AdaptiveResampler&& other) noexcept;
  ~AdaptiveResampler();

  [[nodiscard]] AdaptiveResamplerStatus initialize(
      std::size_t channels,
      AdaptiveResamplerQuality quality = AdaptiveResamplerQuality::fastest) noexcept;
  void reset() noexcept;
  void release() noexcept;

  [[nodiscard]] AdaptiveResamplerProcessResult process(
      std::span<const float> interleaved_input,
      std::uint32_t input_frames,
      std::span<float> interleaved_output,
      std::uint32_t output_frame_capacity,
      double ratio,
      bool end_of_input = false) noexcept;

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::size_t channels() const noexcept;

 private:
  void* state_ = nullptr;
  std::size_t channels_ = 0;
};

}  // namespace sar::realtime
