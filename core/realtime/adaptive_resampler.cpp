#include "core/realtime/adaptive_resampler.h"

#include "samplerate.h"

#include <limits>
#include <utility>

namespace sar::realtime {

namespace {

int converter_type(AdaptiveResamplerQuality quality) noexcept {
  switch (quality) {
    case AdaptiveResamplerQuality::fastest:
      return SRC_SINC_FASTEST;
    case AdaptiveResamplerQuality::medium:
      return SRC_SINC_MEDIUM_QUALITY;
  }
  return SRC_SINC_FASTEST;
}

SRC_STATE* as_src_state(void* state) noexcept {
  return static_cast<SRC_STATE*>(state);
}

bool sample_count_fits(std::size_t sample_count,
                       std::uint32_t frames,
                       std::size_t channels) noexcept {
  return channels != 0 && frames <= std::numeric_limits<long>::max() &&
         static_cast<std::size_t>(frames) <= sample_count / channels;
}

}  // namespace

AdaptiveResampler::AdaptiveResampler(AdaptiveResampler&& other) noexcept
    : state_(std::exchange(other.state_, nullptr)),
      channels_(std::exchange(other.channels_, 0)) {}

AdaptiveResampler& AdaptiveResampler::operator=(AdaptiveResampler&& other) noexcept {
  if (this != &other) {
    release();
    state_ = std::exchange(other.state_, nullptr);
    channels_ = std::exchange(other.channels_, 0);
  }
  return *this;
}

AdaptiveResampler::~AdaptiveResampler() {
  release();
}

AdaptiveResamplerStatus AdaptiveResampler::initialize(
    std::size_t channels,
    AdaptiveResamplerQuality quality) noexcept {
  release();
  if (channels == 0 || channels > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return AdaptiveResamplerStatus::invalid_argument;
  }

  int error = 0;
  state_ = src_new(converter_type(quality), static_cast<int>(channels), &error);
  if (state_ == nullptr || error != 0) {
    release();
    return AdaptiveResamplerStatus::backend_error;
  }

  channels_ = channels;
  return AdaptiveResamplerStatus::success;
}

void AdaptiveResampler::reset() noexcept {
  if (state_ != nullptr) {
    src_reset(as_src_state(state_));
  }
}

void AdaptiveResampler::release() noexcept {
  if (state_ != nullptr) {
    src_delete(as_src_state(state_));
    state_ = nullptr;
  }
  channels_ = 0;
}

AdaptiveResamplerProcessResult AdaptiveResampler::process(
    std::span<const float> interleaved_input,
    std::uint32_t input_frames,
    std::span<float> interleaved_output,
    std::uint32_t output_frame_capacity,
    double ratio,
    bool end_of_input) noexcept {
  if (state_ == nullptr) {
    return {};
  }
  if (!sample_count_fits(interleaved_input.size(), input_frames, channels_) ||
      !sample_count_fits(interleaved_output.size(), output_frame_capacity, channels_) ||
      src_is_valid_ratio(ratio) == 0) {
    return {AdaptiveResamplerStatus::invalid_argument};
  }

  SRC_DATA data{};
  data.data_in = interleaved_input.data();
  data.data_out = interleaved_output.data();
  data.input_frames = static_cast<long>(input_frames);
  data.output_frames = static_cast<long>(output_frame_capacity);
  data.end_of_input = end_of_input ? 1 : 0;
  data.src_ratio = ratio;

  const int error = src_process(as_src_state(state_), &data);
  const auto used = static_cast<std::uint32_t>(data.input_frames_used);
  const auto generated = static_cast<std::uint32_t>(data.output_frames_gen);
  if (error != 0) {
    return {AdaptiveResamplerStatus::backend_error, used, generated, error};
  }
  return {AdaptiveResamplerStatus::success, used, generated, 0};
}

bool AdaptiveResampler::initialized() const noexcept {
  return state_ != nullptr;
}

std::size_t AdaptiveResampler::channels() const noexcept {
  return channels_;
}

}  // namespace sar::realtime
