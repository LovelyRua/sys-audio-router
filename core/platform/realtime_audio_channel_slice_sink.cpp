#include "core/platform/realtime_audio_channel_slice_sink.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sar::platform {

RealtimeAudioChannelSliceSink::RealtimeAudioChannelSliceSink(
    std::size_t first_channel,
    std::size_t channel_count,
    std::size_t frames_per_block,
    RealtimeAudioSink& downstream)
    : first_channel_(first_channel),
      scratch_(channel_count, frames_per_block),
      downstream_(downstream) {
  if (first_channel > std::numeric_limits<std::size_t>::max() - channel_count) {
    throw std::invalid_argument("Audio channel slice range overflows");
  }
}

bool RealtimeAudioChannelSliceSink::write(
    const realtime::AudioBuffer& source) noexcept {
  if (source.channels() < first_channel_ + scratch_.channels() ||
      source.frames() != scratch_.frames()) {
    rejected_blocks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  for (std::size_t channel = 0; channel < scratch_.channels(); ++channel) {
    const auto input = source.channel(first_channel_ + channel);
    auto output = scratch_.channel(channel);
    std::copy(input.begin(), input.end(), output.begin());
  }
  published_blocks_.fetch_add(1, std::memory_order_relaxed);
  if (!downstream_.write(scratch_)) {
    downstream_failures_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  return true;
}

RealtimeAudioChannelSliceSinkStats RealtimeAudioChannelSliceSink::stats()
    const noexcept {
  return {
      published_blocks_.load(std::memory_order_relaxed),
      rejected_blocks_.load(std::memory_order_relaxed),
      downstream_failures_.load(std::memory_order_relaxed),
  };
}

std::size_t RealtimeAudioChannelSliceSink::first_channel() const noexcept {
  return first_channel_;
}

std::size_t RealtimeAudioChannelSliceSink::channel_count() const noexcept {
  return scratch_.channels();
}

}  // namespace sar::platform
