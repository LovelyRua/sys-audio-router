#pragma once

#include "core/platform/virtual_wasapi_transport_layout.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace sar::platform {

enum class VirtualWasapiPacketStatus {
  Completed,
  Empty,
  Full,
  InvalidPacket,
};

struct VirtualWasapiPacket {
  std::uint32_t frames = 0;
  std::uint32_t flags = 0;
  std::uint64_t device_position = 0;
  std::uint64_t qpc_position = 0;
};

struct VirtualWasapiRingCounters {
  std::uint64_t produced_frames = 0;
  std::uint64_t consumed_frames = 0;
  std::uint64_t dropped_frames = 0;
  std::uint64_t silence_frames = 0;
  std::uint64_t malformed_packets = 0;
};

// One producer and one consumer per mapping. Both endpoints must use the same
// generation; attach/detach and mapping initialization belong to the control plane.
class VirtualWasapiTransportRing {
 public:
  static bool initialize(void* memory,
                         std::size_t bytes,
                         const SarVirtualWasapiTransportHeader& header) noexcept {
    if (memory == nullptr ||
        reinterpret_cast<std::uintptr_t>(memory) % SAR_VWASAPI_TRANSPORT_ALIGNMENT != 0 ||
        validate_virtual_wasapi_transport_layout(&header, bytes) != VirtualWasapiLayoutError::None) {
      return false;
    }
    std::memset(memory, 0, header.total_size);
    std::memcpy(memory, &header, sizeof(header));
    return true;
  }

  static std::optional<VirtualWasapiTransportRing> attach(
      void* memory, std::size_t bytes, std::uint32_t direction) noexcept {
    if (memory == nullptr ||
        reinterpret_cast<std::uintptr_t>(memory) % SAR_VWASAPI_TRANSPORT_ALIGNMENT != 0 ||
        bytes < sizeof(SarVirtualWasapiTransportHeader)) {
      return std::nullopt;
    }
    auto* header = static_cast<SarVirtualWasapiTransportHeader*>(memory);
    if (validate_virtual_wasapi_transport_layout(header, bytes, direction) !=
        VirtualWasapiLayoutError::None) {
      return std::nullopt;
    }
    return VirtualWasapiTransportRing(static_cast<std::byte*>(memory), header);
  }

  VirtualWasapiPacketStatus push(std::span<const std::byte> audio,
                                 const VirtualWasapiPacket& packet) noexcept {
    const auto packet_bytes = static_cast<std::size_t>(packet.frames) * bytes_per_frame();
    if (packet.frames == 0 || packet.frames > header_->frames_per_slot ||
        (packet.flags & ~(SAR_VWASAPI_SLOT_FLAG_SILENT |
                          SAR_VWASAPI_SLOT_FLAG_DISCONTINUITY)) != 0 ||
        ((packet.flags & SAR_VWASAPI_SLOT_FLAG_SILENT) == 0
             ? audio.size() != packet_bytes
             : !audio.empty())) {
      std::atomic_ref(state_->malformed_packets).fetch_add(1, std::memory_order_relaxed);
      return VirtualWasapiPacketStatus::InvalidPacket;
    }
    const auto producer = std::atomic_ref(state_->producer_sequence).load(std::memory_order_relaxed);
    const auto consumer = std::atomic_ref(state_->consumer_sequence).load(std::memory_order_acquire);
    if (producer < consumer || producer - consumer >= header_->slot_count) {
      std::atomic_ref(state_->dropped_frames).fetch_add(packet.frames, std::memory_order_relaxed);
      return VirtualWasapiPacketStatus::Full;
    }
    auto* slot = slot_at(producer);
    if ((packet.flags & SAR_VWASAPI_SLOT_FLAG_SILENT) == 0) {
      std::memcpy(audio_at(producer), audio.data(), packet_bytes);
    }
    slot->frame_count = packet.frames;
    slot->flags = packet.flags;
    slot->device_position = packet.device_position;
    slot->qpc_position = packet.qpc_position;
    slot->sequence = producer + 1;
    std::atomic_ref(state_->produced_frames).fetch_add(packet.frames, std::memory_order_relaxed);
    std::atomic_ref(state_->producer_sequence).store(producer + 1, std::memory_order_release);
    return VirtualWasapiPacketStatus::Completed;
  }

  VirtualWasapiPacketStatus pop(std::span<std::byte> audio,
                                VirtualWasapiPacket& packet) noexcept {
    const auto consumer = std::atomic_ref(state_->consumer_sequence).load(std::memory_order_relaxed);
    const auto producer = std::atomic_ref(state_->producer_sequence).load(std::memory_order_acquire);
    if (consumer == producer) return VirtualWasapiPacketStatus::Empty;
    if (consumer > producer || producer - consumer > header_->slot_count) {
      std::atomic_ref(state_->malformed_packets).fetch_add(1, std::memory_order_relaxed);
      return VirtualWasapiPacketStatus::InvalidPacket;
    }
    const auto* slot = slot_at(consumer);
    const auto packet_bytes = static_cast<std::size_t>(slot->frame_count) * bytes_per_frame();
    if (slot->sequence != consumer + 1 || slot->frame_count == 0 ||
        slot->frame_count > header_->frames_per_slot ||
        (slot->flags & ~(SAR_VWASAPI_SLOT_FLAG_SILENT |
                         SAR_VWASAPI_SLOT_FLAG_DISCONTINUITY)) != 0 ||
        audio.size() < packet_bytes) {
      std::atomic_ref(state_->malformed_packets).fetch_add(1, std::memory_order_relaxed);
      return VirtualWasapiPacketStatus::InvalidPacket;
    }
    packet = {slot->frame_count, slot->flags, slot->device_position, slot->qpc_position};
    if ((slot->flags & SAR_VWASAPI_SLOT_FLAG_SILENT) != 0) {
      std::memset(audio.data(), 0, packet_bytes);
      std::atomic_ref(state_->silence_frames).fetch_add(slot->frame_count,
                                                         std::memory_order_relaxed);
    } else {
      std::memcpy(audio.data(), audio_at(consumer), packet_bytes);
    }
    std::atomic_ref(state_->consumed_frames).fetch_add(slot->frame_count,
                                                        std::memory_order_relaxed);
    std::atomic_ref(state_->consumer_sequence).store(consumer + 1, std::memory_order_release);
    return VirtualWasapiPacketStatus::Completed;
  }

  VirtualWasapiRingCounters counters() const noexcept {
    return {
        std::atomic_ref(state_->produced_frames).load(std::memory_order_relaxed),
        std::atomic_ref(state_->consumed_frames).load(std::memory_order_relaxed),
        std::atomic_ref(state_->dropped_frames).load(std::memory_order_relaxed),
        std::atomic_ref(state_->silence_frames).load(std::memory_order_relaxed),
        std::atomic_ref(state_->malformed_packets).load(std::memory_order_relaxed),
    };
  }

 private:
  VirtualWasapiTransportRing(std::byte* memory,
                             SarVirtualWasapiTransportHeader* header) noexcept
      : memory_(memory),
        header_(header),
        state_(reinterpret_cast<SarVirtualWasapiRingState*>(
            memory + header->ring_state_offset)) {}

  std::size_t bytes_per_frame() const noexcept {
    return static_cast<std::size_t>(header_->channel_count) * (header_->bits_per_sample / 8);
  }
  SarVirtualWasapiSlotState* slot_at(std::uint64_t sequence) const noexcept {
    return reinterpret_cast<SarVirtualWasapiSlotState*>(
        memory_ + header_->slot_table_offset +
        (sequence % header_->slot_count) * header_->slot_stride);
  }
  std::byte* audio_at(std::uint64_t sequence) const noexcept {
    return memory_ + header_->audio_data_offset +
           (sequence % header_->slot_count) * header_->audio_slot_stride;
  }

  std::byte* memory_;
  SarVirtualWasapiTransportHeader* header_;
  SarVirtualWasapiRingState* state_;
};

}  // namespace sar::platform
