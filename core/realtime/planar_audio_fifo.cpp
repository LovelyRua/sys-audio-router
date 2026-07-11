#include "core/realtime/planar_audio_fifo.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sar::realtime {
namespace {

std::size_t checked_sample_count(std::size_t channels,
                                 std::size_t capacity_frames) {
  if (channels == 0 || capacity_frames == 0) {
    throw std::invalid_argument(
        "PlanarAudioFifo requires non-zero channels and capacity");
  }
  if (capacity_frames > std::numeric_limits<std::size_t>::max() / channels) {
    throw std::length_error("PlanarAudioFifo sample count overflows size_t");
  }
  return channels * capacity_frames;
}

}  // namespace

PlanarAudioFifo::PlanarAudioFifo(std::size_t channels,
                                 std::size_t capacity_frames)
    : channels_(channels),
      capacity_frames_(capacity_frames),
      samples_(checked_sample_count(channels, capacity_frames), 0.0F) {}

std::size_t PlanarAudioFifo::channels() const noexcept {
  return channels_;
}

std::size_t PlanarAudioFifo::capacity_frames() const noexcept {
  return capacity_frames_;
}

std::size_t PlanarAudioFifo::available_frames() const noexcept {
  return available_frames_;
}

std::size_t PlanarAudioFifo::free_frames() const noexcept {
  return capacity_frames_ - available_frames_;
}

std::size_t PlanarAudioFifo::push(const AudioBuffer& source,
                                  std::size_t frames) noexcept {
  if (source.channels() != channels_) {
    return 0;
  }

  const auto transferred =
      std::min({frames, source.frames(), free_frames()});
  const auto first = std::min(transferred, capacity_frames_ - write_frame_);
  const auto second = transferred - first;

  for (std::size_t channel_index = 0; channel_index < channels_;
       ++channel_index) {
    const auto source_channel = source.channel(channel_index);
    auto* fifo_channel = samples_.data() + channel_index * capacity_frames_;
    std::copy_n(source_channel.data(), first, fifo_channel + write_frame_);
    std::copy_n(source_channel.data() + first, second, fifo_channel);
  }

  write_frame_ = (write_frame_ + transferred) % capacity_frames_;
  available_frames_ += transferred;
  return transferred;
}

std::size_t PlanarAudioFifo::pop(AudioBuffer& destination,
                                 std::size_t frames) noexcept {
  if (destination.channels() != channels_) {
    return 0;
  }

  const auto transferred =
      std::min({frames, destination.frames(), available_frames_});
  const auto first = std::min(transferred, capacity_frames_ - read_frame_);
  const auto second = transferred - first;

  for (std::size_t channel_index = 0; channel_index < channels_;
       ++channel_index) {
    const auto* fifo_channel =
        samples_.data() + channel_index * capacity_frames_;
    auto destination_channel = destination.channel(channel_index);
    std::copy_n(fifo_channel + read_frame_, first, destination_channel.data());
    std::copy_n(fifo_channel, second, destination_channel.data() + first);
  }

  read_frame_ = (read_frame_ + transferred) % capacity_frames_;
  available_frames_ -= transferred;
  return transferred;
}

void PlanarAudioFifo::clear() noexcept {
  read_frame_ = 0;
  write_frame_ = 0;
  available_frames_ = 0;
}

}  // namespace sar::realtime
