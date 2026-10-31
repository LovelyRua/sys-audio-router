#pragma once

#include "core/realtime/audio_buffer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace sar::tools {

// Received-frame offsets, not device timestamps or continuity claims. Storage
// is allocated once before capture; observing a packet never grows it.
class CaptureLevelEvidence {
 public:
  static constexpr std::uint64_t kNoFrame = std::numeric_limits<std::uint64_t>::max();
  static constexpr float kSignalThreshold = 1.0e-5F;

  explicit CaptureLevelEvidence(std::size_t channels = 0)
      : previous_(channels, 0.0F), previous_valid_(channels, false) {}

  bool observe(const realtime::AudioBuffer& buffer, std::size_t count) noexcept {
    if (count > buffer.frames() || buffer.channels() != previous_.size() ||
        (count != 0 && buffer.channels() == 0)) return false;
    if (count == 0) return true;
    const auto packet = packets++;
    bool packet_signal = false;
    for (std::size_t frame = 0; frame < count; ++frame) {
      bool frame_nonzero = false;
      bool frame_signal = false;
      const auto offset = frames + frame;
      for (std::size_t channel = 0; channel < buffer.channels(); ++channel) {
        const auto sample = buffer.channel(channel)[frame];
        if (!std::isfinite(sample)) {
          ++nonfinite;
          previous_valid_[channel] = false;
          continue;
        }
        ++finite_samples;
        const auto magnitude = std::fabs(sample);
        if (magnitude > peak) { peak = magnitude; peak_frame = offset; }
        square_sum_ += static_cast<double>(sample) * sample;
        if (sample != 0.0F) { ++nonzero_samples; frame_nonzero = true; }
        frame_signal = frame_signal || magnitude >= kSignalThreshold;
        if (previous_valid_[channel]) {
          const auto step = std::fabs(static_cast<double>(sample) - previous_[channel]);
          if (step > maximum_step) {
            maximum_step = step;
            maximum_step_frame = offset;
            maximum_step_channel = channel;
          }
        }
        previous_[channel] = sample;
        previous_valid_[channel] = true;
      }
      if (frame_nonzero) ++nonzero_frames;
      if (frame_signal) {
        ++signal_frames;
        if (first_signal_frame == kNoFrame) {
          first_signal_frame = offset;
          first_signal_packet = packet;
        }
        last_signal_frame = offset;
        packet_signal = true;
      }
    }
    if (packet_signal) ++signal_packets;
    frames += count;
    return true;
  }

  double rms() const noexcept {
    return finite_samples == 0 ? 0.0 : std::sqrt(square_sum_ / finite_samples);
  }

  std::uint64_t frames = 0;
  std::uint64_t packets = 0;
  std::uint64_t finite_samples = 0;
  std::uint64_t nonfinite = 0;
  std::uint64_t nonzero_samples = 0;
  std::uint64_t nonzero_frames = 0;
  std::uint64_t signal_frames = 0;
  std::uint64_t signal_packets = 0;
  std::uint64_t first_signal_frame = kNoFrame;
  std::uint64_t last_signal_frame = kNoFrame;
  std::uint64_t first_signal_packet = kNoFrame;
  std::uint64_t peak_frame = kNoFrame;
  std::uint64_t maximum_step_frame = kNoFrame;
  std::size_t maximum_step_channel = 0;
  float peak = 0.0F;
  double maximum_step = 0.0;

 private:
  double square_sum_ = 0.0;
  std::vector<float> previous_;
  std::vector<bool> previous_valid_;
};

}  // namespace sar::tools
