#pragma once

#include "core/platform/realtime_audio_source.h"
#include "core/platform/virtual_wasapi_transport_ring.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sar::platform {

// The mapping outlives this adapter. One adapter owns its consumer cursor;
// construction and allocation happen before the realtime graph starts.
class VirtualWasapiRingAudioSource final : public RealtimeAudioQueuedSource {
 public:
  explicit VirtualWasapiRingAudioSource(VirtualWasapiTransportRing ring)
      : ring_(std::move(ring)),
        packet_(static_cast<std::size_t>(ring_.frames_per_slot()) *
                ring_.channel_count() * sizeof(float)) {
    if (ring_.direction() != SAR_VWASAPI_DIRECTION_RENDER ||
        ring_.sample_format() != SAR_VWASAPI_SAMPLE_IEEE_FLOAT ||
        ring_.bits_per_sample() != 32) {
      throw std::invalid_argument("Virtual WASAPI source requires float32 render transport");
    }
  }

  VirtualWasapiRingAudioSource(const VirtualWasapiRingAudioSource&) = delete;
  VirtualWasapiRingAudioSource& operator=(const VirtualWasapiRingAudioSource&) = delete;

  [[nodiscard]] bool read(realtime::AudioBuffer& destination) noexcept override {
    destination.clear();
    if (destination.channels() != ring_.channel_count()) {
      silent_reads_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }

    std::size_t written = 0;
    while (written < destination.frames()) {
      if (packet_offset_ == packet_frames_) {
        VirtualWasapiPacket packet;
        const auto status = ring_.pop(packet_, packet);
        if (status != VirtualWasapiPacketStatus::Completed) {
          if (status == VirtualWasapiPacketStatus::InvalidPacket) {
            malformed_reads_.fetch_add(1, std::memory_order_relaxed);
          }
          break;
        }
        packet_frames_ = packet.frames;
        packet_offset_ = 0;
        pending_frames_.store(packet.frames, std::memory_order_relaxed);
        consumed_blocks_.fetch_add(1, std::memory_order_relaxed);
      }

      const auto frames = std::min(destination.frames() - written,
                                   packet_frames_ - packet_offset_);
      for (std::size_t frame = 0; frame < frames; ++frame) {
        for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
          float sample = 0.0F;
          const auto offset = ((packet_offset_ + frame) * destination.channels() +
                               channel) * sizeof(float);
          std::memcpy(&sample, packet_.data() + offset, sizeof(sample));
          if (!std::isfinite(sample)) {
            sample = 0.0F;
            non_finite_samples_.fetch_add(1, std::memory_order_relaxed);
          }
          destination.channel(channel)[written + frame] = sample;
        }
      }
      packet_offset_ += frames;
      pending_frames_.store(packet_frames_ - packet_offset_, std::memory_order_relaxed);
      written += frames;
    }

    if (written < destination.frames()) {
      producer_underflows_.fetch_add(1, std::memory_order_relaxed);
    }
    if (written == 0) {
      silent_reads_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    return true;
  }

  [[nodiscard]] std::size_t available_frames() const noexcept override {
    return pending_frames_.load(std::memory_order_relaxed) +
           ring_.queued_slots() * ring_.frames_per_slot();
  }

  [[nodiscard]] RealtimeAudioSourceDiagnostics diagnostics() const noexcept override {
    RealtimeAudioSourceDiagnostics result;
    result.consumed_blocks = consumed_blocks_.load(std::memory_order_relaxed);
    result.producer_underflows = producer_underflows_.load(std::memory_order_relaxed);
    result.silent_reads = silent_reads_.load(std::memory_order_relaxed);
    result.non_finite_samples = non_finite_samples_.load(std::memory_order_relaxed);
    result.dropped_blocks = malformed_reads_.load(std::memory_order_relaxed);
    result.producer_overflows = ring_.counters().dropped_frames / ring_.frames_per_slot();
    return result;
  }

 private:
  VirtualWasapiTransportRing ring_;
  std::vector<std::byte> packet_;
  std::size_t packet_frames_ = 0;
  std::size_t packet_offset_ = 0;
  std::atomic<std::size_t> pending_frames_ = 0;
  std::atomic<std::uint64_t> consumed_blocks_ = 0;
  std::atomic<std::uint64_t> malformed_reads_ = 0;
  std::atomic<std::uint64_t> producer_underflows_ = 0;
  std::atomic<std::uint64_t> silent_reads_ = 0;
  std::atomic<std::uint64_t> non_finite_samples_ = 0;
};

}  // namespace sar::platform
