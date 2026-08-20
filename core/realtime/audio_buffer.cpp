#include "core/realtime/audio_buffer.h"

#include <algorithm>
#include <stdexcept>

namespace sar::realtime {

AudioBuffer::AudioBuffer(std::size_t channels, std::size_t frames)
    : channels_(channels), frames_(frames), samples_(channels * frames, 0.0F) {
  if (frames == 0) {
    throw std::invalid_argument("AudioBuffer requires a non-zero frame count");
  }
}

std::size_t AudioBuffer::channels() const noexcept {
  return channels_;
}

std::size_t AudioBuffer::frames() const noexcept {
  return frames_;
}

std::span<float> AudioBuffer::channel(std::size_t index) noexcept {
  return {samples_.data() + (index * frames_), frames_};
}

std::span<const float> AudioBuffer::channel(std::size_t index) const noexcept {
  return {samples_.data() + (index * frames_), frames_};
}

void AudioBuffer::clear() noexcept {
  std::ranges::fill(samples_, 0.0F);
}

void AudioBuffer::copy_from(const AudioBuffer& source) noexcept {
  clear();

  const auto copy_channels = std::min(channels_, source.channels_);
  const auto copy_frames = std::min(frames_, source.frames_);

  for (std::size_t channel_index = 0; channel_index < copy_channels; ++channel_index) {
    const auto source_channel = source.channel(channel_index);
    auto destination_channel = channel(channel_index);
    std::copy_n(source_channel.begin(), copy_frames, destination_channel.begin());
  }
}

}  // namespace sar::realtime
