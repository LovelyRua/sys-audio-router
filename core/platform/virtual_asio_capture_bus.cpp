#include "core/platform/virtual_asio_capture_bus.h"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sar::platform {
namespace {

constexpr std::uint8_t kIdle = 0;
constexpr std::uint8_t kClaiming = 1;
constexpr std::uint8_t kActive = 2;

template <typename T>
void update_max(std::atomic<T>& target, T value) noexcept {
  auto current = target.load(std::memory_order_relaxed);
  while (current < value &&
         !target.compare_exchange_weak(current, value,
                                       std::memory_order_relaxed)) {
  }
}

}  // namespace

VirtualAsioCaptureConsumer::VirtualAsioCaptureConsumer(
    VirtualAsioCaptureBus* bus,
    std::size_t slot,
    std::uint64_t generation) noexcept
    : bus_(bus), slot_(slot), generation_(generation) {}

VirtualAsioCaptureConsumer::VirtualAsioCaptureConsumer(
    VirtualAsioCaptureConsumer&& other) noexcept
    : bus_(std::exchange(other.bus_, nullptr)),
      slot_(other.slot_),
      generation_(other.generation_) {}

VirtualAsioCaptureConsumer& VirtualAsioCaptureConsumer::operator=(
    VirtualAsioCaptureConsumer&& other) noexcept {
  if (this != &other) {
    reset();
    bus_ = std::exchange(other.bus_, nullptr);
    slot_ = other.slot_;
    generation_ = other.generation_;
  }
  return *this;
}

VirtualAsioCaptureConsumer::~VirtualAsioCaptureConsumer() {
  reset();
}

bool VirtualAsioCaptureConsumer::valid() const noexcept {
  return bus_ != nullptr;
}

std::size_t VirtualAsioCaptureConsumer::available_frames() const noexcept {
  return bus_ != nullptr ? bus_->available_frames(slot_, generation_) : 0;
}

bool VirtualAsioCaptureConsumer::read(
    realtime::AudioBuffer& destination) noexcept {
  return bus_ != nullptr && bus_->read(slot_, generation_, destination);
}

void VirtualAsioCaptureConsumer::reset() noexcept {
  if (bus_ != nullptr) {
    bus_->detach(slot_, generation_);
    bus_ = nullptr;
  }
}

VirtualAsioCaptureBus::Slot::Slot(std::size_t channels,
                                 std::size_t producer_frames,
                                 std::size_t queue_capacity_blocks)
    : pending(channels,
              kVirtualAsioMaxFramesPerBlock +
                  producer_frames * queue_capacity_blocks) {
  blocks.reserve(queue_capacity_blocks + 1);
  for (std::size_t index = 0; index <= queue_capacity_blocks; ++index) {
    blocks.emplace_back(channels, producer_frames);
  }
}

VirtualAsioCaptureBus::VirtualAsioCaptureBus(
    std::size_t channels,
    std::size_t producer_frames,
    std::size_t maximum_consumers,
    std::size_t queue_capacity_blocks)
    : channels_(channels), producer_frames_(producer_frames) {
  if (channels == 0 || producer_frames == 0 || maximum_consumers == 0 ||
      queue_capacity_blocks == 0) {
    throw std::invalid_argument(
        "Virtual ASIO capture bus dimensions must be non-zero");
  }
  slots_.reserve(maximum_consumers);
  for (std::size_t index = 0; index < maximum_consumers; ++index) {
    slots_.push_back(std::make_unique<Slot>(
        channels, producer_frames, queue_capacity_blocks));
  }
}

VirtualAsioCaptureConsumer VirtualAsioCaptureBus::attach() {
  std::lock_guard lock(attachment_mutex_);
  for (std::size_t index = 0; index < slots_.size(); ++index) {
    auto& slot = *slots_[index];
    std::uint8_t expected = kIdle;
    if (!slot.state.compare_exchange_strong(expected, kClaiming,
                                            std::memory_order_acq_rel)) {
      continue;
    }
    while (slot.producer_writing.load(std::memory_order_acquire) ||
           slot.consumer_reading.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    slot.read_index.store(0, std::memory_order_relaxed);
    slot.write_index.store(0, std::memory_order_relaxed);
    slot.pending.clear();
    const auto generation =
        next_generation_.fetch_add(1, std::memory_order_relaxed);
    slot.generation.store(generation, std::memory_order_relaxed);
    slot.state.store(kActive, std::memory_order_release);
    active_consumers_.fetch_add(1, std::memory_order_relaxed);
    return {this, index, generation};
  }
  return {};
}

bool VirtualAsioCaptureBus::write(
    const realtime::AudioBuffer& source) noexcept {
  if (source.channels() != channels_ || source.frames() != producer_frames_) {
    dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  bool published = false;
  for (const auto& owned_slot : slots_) {
    auto& slot = *owned_slot;
    if (slot.state.load(std::memory_order_acquire) != kActive) {
      continue;
    }
    slot.producer_writing.store(true, std::memory_order_release);
    if (slot.state.load(std::memory_order_acquire) != kActive) {
      slot.producer_writing.store(false, std::memory_order_release);
      continue;
    }
    const auto write = slot.write_index.load(std::memory_order_relaxed);
    const auto next = increment(slot, write);
    const auto read = slot.read_index.load(std::memory_order_acquire);
    if (next == read) {
      consumer_overflows_.fetch_add(1, std::memory_order_relaxed);
      dropped_blocks_.fetch_add(1, std::memory_order_relaxed);
      slot.producer_writing.store(false, std::memory_order_release);
      continue;
    }
    slot.blocks[write].audio.copy_from(source);
    slot.write_index.store(next, std::memory_order_release);
    update_max(maximum_queue_depth_, queue_depth(slot, read, next));
    published_blocks_.fetch_add(1, std::memory_order_relaxed);
    published = true;
    slot.producer_writing.store(false, std::memory_order_release);
  }
  return published;
}

bool VirtualAsioCaptureBus::read(
    std::size_t slot_index,
    std::uint64_t generation,
    realtime::AudioBuffer& destination) noexcept {
  destination.clear();
  if (slot_index >= slots_.size() || destination.channels() != channels_ ||
      destination.frames() == 0 ||
      destination.frames() > kVirtualAsioMaxFramesPerBlock) {
    consumer_underflows_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  auto& slot = *slots_[slot_index];
  if (slot.state.load(std::memory_order_acquire) != kActive ||
      slot.generation.load(std::memory_order_relaxed) != generation) {
    consumer_underflows_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  slot.consumer_reading.store(true, std::memory_order_release);
  if (slot.state.load(std::memory_order_acquire) != kActive ||
      slot.generation.load(std::memory_order_relaxed) != generation) {
    slot.consumer_reading.store(false, std::memory_order_release);
    consumer_underflows_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  while (slot.pending.available_frames() < destination.frames()) {
    const auto read = slot.read_index.load(std::memory_order_relaxed);
    if (read == slot.write_index.load(std::memory_order_acquire)) {
      break;
    }
    const auto& block = slot.blocks[read].audio;
    if (slot.pending.free_frames() < block.frames() ||
        slot.pending.push(block, block.frames()) != block.frames()) {
      consumer_overflows_.fetch_add(1, std::memory_order_relaxed);
      break;
    }
    slot.read_index.store(increment(slot, read), std::memory_order_release);
    consumed_blocks_.fetch_add(1, std::memory_order_relaxed);
  }
  if (slot.pending.available_frames() < destination.frames()) {
    slot.consumer_reading.store(false, std::memory_order_release);
    consumer_underflows_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  const auto completed = slot.pending.pop(destination, destination.frames()) ==
                         destination.frames();
  slot.consumer_reading.store(false, std::memory_order_release);
  return completed;
}

std::size_t VirtualAsioCaptureBus::available_frames(
    std::size_t slot_index,
    std::uint64_t generation) const noexcept {
  if (slot_index >= slots_.size()) {
    return 0;
  }
  const auto& slot = *slots_[slot_index];
  if (slot.state.load(std::memory_order_acquire) != kActive ||
      slot.generation.load(std::memory_order_relaxed) != generation) {
    return 0;
  }
  const auto read = slot.read_index.load(std::memory_order_acquire);
  const auto write = slot.write_index.load(std::memory_order_acquire);
  return slot.pending.available_frames() +
         queue_depth(slot, read, write) * producer_frames_;
}

void VirtualAsioCaptureBus::detach(std::size_t slot_index,
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
  while (slot.producer_writing.load(std::memory_order_acquire) ||
         slot.consumer_reading.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  slot.read_index.store(0, std::memory_order_relaxed);
  slot.write_index.store(0, std::memory_order_relaxed);
  slot.pending.clear();
  slot.state.store(kIdle, std::memory_order_release);
  active_consumers_.fetch_sub(1, std::memory_order_relaxed);
}

VirtualAsioCaptureBusStats VirtualAsioCaptureBus::stats() const noexcept {
  return {
      published_blocks_.load(std::memory_order_relaxed),
      dropped_blocks_.load(std::memory_order_relaxed),
      consumed_blocks_.load(std::memory_order_relaxed),
      consumer_underflows_.load(std::memory_order_relaxed),
      consumer_overflows_.load(std::memory_order_relaxed),
      maximum_queue_depth_.load(std::memory_order_relaxed),
      active_consumers_.load(std::memory_order_relaxed),
  };
}

std::size_t VirtualAsioCaptureBus::channels() const noexcept {
  return channels_;
}

std::size_t VirtualAsioCaptureBus::producer_frames() const noexcept {
  return producer_frames_;
}

std::size_t VirtualAsioCaptureBus::increment(const Slot& slot,
                                             std::size_t index) const noexcept {
  return (index + 1) % slot.blocks.size();
}

std::size_t VirtualAsioCaptureBus::queue_depth(
    const Slot& slot,
    std::size_t read,
    std::size_t write) const noexcept {
  return write >= read ? write - read : slot.blocks.size() - read + write;
}

}  // namespace sar::platform
