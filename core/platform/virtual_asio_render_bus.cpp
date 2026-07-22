#include "core/platform/virtual_asio_render_bus.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sar::platform {
namespace {

constexpr std::uint8_t kIdle = 0;
constexpr std::uint8_t kClaiming = 1;
constexpr std::uint8_t kActive = 2;

}  // namespace

VirtualAsioRenderProducer::VirtualAsioRenderProducer(
    VirtualAsioRenderBus* bus,
    std::size_t slot,
    std::uint64_t generation) noexcept
    : bus_(bus), slot_(slot), generation_(generation) {}

VirtualAsioRenderProducer::VirtualAsioRenderProducer(
    VirtualAsioRenderProducer&& other) noexcept
    : bus_(std::exchange(other.bus_, nullptr)),
      slot_(other.slot_),
      generation_(other.generation_) {}

VirtualAsioRenderProducer& VirtualAsioRenderProducer::operator=(
    VirtualAsioRenderProducer&& other) noexcept {
  if (this != &other) {
    reset();
    bus_ = std::exchange(other.bus_, nullptr);
    slot_ = other.slot_;
    generation_ = other.generation_;
  }
  return *this;
}

VirtualAsioRenderProducer::~VirtualAsioRenderProducer() {
  reset();
}

bool VirtualAsioRenderProducer::valid() const noexcept {
  return bus_ != nullptr;
}

bool VirtualAsioRenderProducer::push(
    const realtime::AudioBuffer& source) noexcept {
  return bus_ != nullptr && bus_->push(slot_, generation_, source);
}

void VirtualAsioRenderProducer::reset() noexcept {
  if (bus_ != nullptr) {
    bus_->detach(slot_, generation_);
    bus_ = nullptr;
  }
}

VirtualAsioRenderBus::Slot::Slot(std::size_t channels,
                                std::size_t frames,
                                std::size_t queue_capacity_blocks) {
  blocks.reserve(queue_capacity_blocks + 1);
  for (std::size_t index = 0; index <= queue_capacity_blocks; ++index) {
    blocks.emplace_back(channels, frames);
  }
}

VirtualAsioRenderBus::VirtualAsioRenderBus(
    std::size_t channels,
    std::size_t frames,
    std::size_t maximum_producers,
    std::size_t queue_capacity_blocks)
    : channels_(channels), frames_(frames) {
  if (channels == 0 || frames == 0 || maximum_producers == 0 ||
      queue_capacity_blocks == 0) {
    throw std::invalid_argument("Virtual ASIO render bus dimensions must be non-zero");
  }
  slots_.reserve(maximum_producers);
  for (std::size_t index = 0; index < maximum_producers; ++index) {
    slots_.push_back(
        std::make_unique<Slot>(channels, frames, queue_capacity_blocks));
  }
}

VirtualAsioRenderProducer VirtualAsioRenderBus::attach() {
  std::lock_guard lock(attachment_mutex_);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    auto& slot = *slots_[index];
    std::uint8_t expected = kIdle;
    if (!slot.state.compare_exchange_strong(expected, kClaiming,
                                            std::memory_order_acq_rel)) {
      continue;
    }
    while (slot.consumer_reading.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    slot.read_index.store(0, std::memory_order_relaxed);
    slot.write_index.store(0, std::memory_order_relaxed);
    const auto generation =
        next_generation_.fetch_add(1, std::memory_order_relaxed);
    slot.generation.store(generation, std::memory_order_relaxed);
    slot.state.store(kActive, std::memory_order_release);
    active_producers_.fetch_add(1, std::memory_order_relaxed);
    return {this, index, generation};
  }
  return {};
}

bool VirtualAsioRenderBus::read(
    realtime::AudioBuffer& destination) noexcept {
  destination.clear();
  if (destination.channels() != channels_ || destination.frames() != frames_) {
    silent_reads_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  bool mixed = false;
  for (const auto& owned_slot : slots_) {
    auto& slot = *owned_slot;
    if (slot.state.load(std::memory_order_acquire) != kActive) {
      continue;
    }
    slot.consumer_reading.store(true, std::memory_order_release);
    if (slot.state.load(std::memory_order_acquire) != kActive) {
      slot.consumer_reading.store(false, std::memory_order_release);
      continue;
    }

    const auto read_index = slot.read_index.load(std::memory_order_relaxed);
    if (read_index != slot.write_index.load(std::memory_order_acquire)) {
      const auto& source = slot.blocks[read_index].audio;
      for (std::size_t channel = 0; channel < channels_; ++channel) {
        const auto input = source.channel(channel);
        auto output = destination.channel(channel);
        for (std::size_t frame = 0; frame < frames_; ++frame) {
          output[frame] += input[frame];
        }
      }
      slot.read_index.store(increment(slot, read_index),
                            std::memory_order_release);
      mixed = true;
    }
    slot.consumer_reading.store(false, std::memory_order_release);
  }

  if (mixed) {
    mixed_blocks_.fetch_add(1, std::memory_order_relaxed);
  } else {
    silent_reads_.fetch_add(1, std::memory_order_relaxed);
  }
  return mixed;
}

VirtualAsioRenderBusStats VirtualAsioRenderBus::stats() const noexcept {
  return {
      pushed_blocks_.load(std::memory_order_relaxed),
      dropped_blocks_.load(std::memory_order_relaxed),
      mixed_blocks_.load(std::memory_order_relaxed),
      silent_reads_.load(std::memory_order_relaxed),
      active_producers_.load(std::memory_order_relaxed),
  };
}

std::size_t VirtualAsioRenderBus::channels() const noexcept {
  return channels_;
}

std::size_t VirtualAsioRenderBus::frames() const noexcept {
  return frames_;
}

bool VirtualAsioRenderBus::push(
    std::size_t slot_index,
    std::uint64_t generation,
    const realtime::AudioBuffer& source) noexcept {
  if (slot_index >= slots_.size() || source.channels() != channels_ ||
      source.frames() != frames_) {
    dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  auto& slot = *slots_[slot_index];
  if (slot.state.load(std::memory_order_acquire) != kActive ||
      slot.generation.load(std::memory_order_relaxed) != generation) {
    dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  const auto write_index = slot.write_index.load(std::memory_order_relaxed);
  const auto next = increment(slot, write_index);
  if (next == slot.read_index.load(std::memory_order_acquire)) {
    dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  slot.blocks[write_index].audio.copy_from(source);
  slot.write_index.store(next, std::memory_order_release);
  pushed_blocks_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void VirtualAsioRenderBus::detach(std::size_t slot_index,
                                  std::uint64_t generation) noexcept {
  if (slot_index >= slots_.size()) {
    return;
  }
  std::lock_guard lock(attachment_mutex_);
  auto& slot = *slots_[slot_index];
  if (slot.generation.load(std::memory_order_relaxed) != generation ||
      slot.state.exchange(kClaiming, std::memory_order_acq_rel) != kActive) {
    return;
  }
  while (slot.consumer_reading.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  slot.read_index.store(0, std::memory_order_relaxed);
  slot.write_index.store(0, std::memory_order_relaxed);
  slot.state.store(kIdle, std::memory_order_release);
  active_producers_.fetch_sub(1, std::memory_order_relaxed);
}

std::size_t VirtualAsioRenderBus::increment(const Slot& slot,
                                            std::size_t index) const noexcept {
  return (index + 1) % slot.blocks.size();
}

}  // namespace sar::platform
