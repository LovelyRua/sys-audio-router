#include "core/platform/windows_virtual_asio_shared_queue.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace sar::platform {
namespace {

std::uint64_t atomic_load(const std::uint64_t& value) noexcept {
  auto* target = reinterpret_cast<volatile LONG64*>(
      const_cast<std::uint64_t*>(&value));
  return static_cast<std::uint64_t>(InterlockedCompareExchange64(target, 0, 0));
}

void atomic_store(std::uint64_t& target, std::uint64_t value) noexcept {
  InterlockedExchange64(reinterpret_cast<volatile LONG64*>(&target),
                        static_cast<LONG64>(value));
}

void atomic_increment(std::uint64_t& target) noexcept {
  InterlockedIncrement64(reinterpret_cast<volatile LONG64*>(&target));
}

}  // namespace

WindowsVirtualAsioSharedQueueBindResult
WindowsVirtualAsioSharedQueueBindResult::success(
    WindowsVirtualAsioSharedQueue queue) {
  return {std::move(queue), nullptr};
}

WindowsVirtualAsioSharedQueueBindResult
WindowsVirtualAsioSharedQueueBindResult::failure(const char* error_code) {
  return {std::nullopt, error_code};
}

bool WindowsVirtualAsioSharedQueueBindResult::ok() const noexcept {
  return queue_.has_value();
}

WindowsVirtualAsioSharedQueue&
WindowsVirtualAsioSharedQueueBindResult::queue() noexcept {
  return *queue_;
}

WindowsVirtualAsioSharedQueue
WindowsVirtualAsioSharedQueueBindResult::take_queue() noexcept {
  return std::move(*queue_);
}

const char* WindowsVirtualAsioSharedQueueBindResult::error_code() const noexcept {
  return error_code_;
}

WindowsVirtualAsioSharedQueueBindResult::
    WindowsVirtualAsioSharedQueueBindResult(
        std::optional<WindowsVirtualAsioSharedQueue> queue,
        const char* error_code)
    : queue_(std::move(queue)), error_code_(error_code) {}

WindowsVirtualAsioSharedQueueBindResult WindowsVirtualAsioSharedQueue::bind(
    WindowsVirtualAsioSharedMemory& mapping,
    VirtualAsioSharedQueueDirection direction) noexcept {
  if (!mapping.valid()) {
    return WindowsVirtualAsioSharedQueueBindResult::failure(
        "virtual_asio_mapping_not_open");
  }
  const auto& queue = direction == VirtualAsioSharedQueueDirection::Input
                          ? mapping.layout().input_queue
                          : mapping.layout().output_queue;
  if (!queue.present()) {
    return WindowsVirtualAsioSharedQueueBindResult::failure(
        "virtual_asio_queue_direction_unavailable");
  }
  auto* base = static_cast<std::byte*>(mapping.data());
  auto* header = reinterpret_cast<VirtualAsioSharedMemoryHeader*>(base);
  auto* control = reinterpret_cast<VirtualAsioSharedQueueControl*>(
      base + queue.control_offset);
  return WindowsVirtualAsioSharedQueueBindResult::success(
      WindowsVirtualAsioSharedQueue(header,
                                    control,
                                    base + queue.slots_offset,
                                    queue,
                                    mapping.header().frames_per_block,
                                    mapping.header().queue_capacity_blocks,
                                    mapping.header().connection_generation));
}

std::uint32_t WindowsVirtualAsioSharedQueue::channels() const noexcept {
  return layout_.channels;
}

std::uint32_t WindowsVirtualAsioSharedQueue::frames_per_block() const noexcept {
  return frames_;
}

std::uint32_t WindowsVirtualAsioSharedQueue::capacity_blocks() const noexcept {
  return capacity_;
}

std::uint64_t WindowsVirtualAsioSharedQueue::connection_generation()
    const noexcept {
  return generation_;
}

VirtualAsioSharedQueueStatus WindowsVirtualAsioSharedQueue::push(
    const realtime::AudioBuffer& source,
    const VirtualAsioSharedBlockMetadata& metadata) noexcept {
  if (!ready()) {
    return VirtualAsioSharedQueueStatus::NotReady;
  }
  if (!matches(source)) {
    return VirtualAsioSharedQueueStatus::FormatMismatch;
  }
  const auto write = atomic_load(control_->write_position);
  const auto read = atomic_load(control_->read_position);
  if (write - read >= capacity_) {
    atomic_increment(control_->overrun_blocks);
    return VirtualAsioSharedQueueStatus::Full;
  }

  auto* block = slot(write);
  auto* block_header = reinterpret_cast<VirtualAsioSharedBlockHeader*>(block);
  auto* samples = reinterpret_cast<float*>(
      block + sizeof(VirtualAsioSharedBlockHeader));
  for (std::uint32_t channel = 0; channel < channels(); ++channel) {
    const auto source_samples = source.channel(channel);
    std::memcpy(samples + static_cast<std::size_t>(channel) * frames_,
                source_samples.data(),
                static_cast<std::size_t>(frames_) * sizeof(float));
  }
  block_header->sequence = metadata.sequence;
  block_header->connection_generation = generation_;
  block_header->sample_position = metadata.sample_position;
  block_header->qpc_position_100ns = metadata.qpc_position_100ns;
  block_header->valid_frames = frames_;
  block_header->flags = metadata.flags;
  block_header->reserved[0] = 0;
  block_header->reserved[1] = 0;
  block_header->reserved[2] = 0;
  atomic_store(control_->write_position, write + 1);
  atomic_increment(control_->produced_blocks);
  return VirtualAsioSharedQueueStatus::Completed;
}

VirtualAsioSharedQueueStatus WindowsVirtualAsioSharedQueue::pop(
    realtime::AudioBuffer& destination,
    VirtualAsioSharedBlockMetadata& metadata) noexcept {
  if (!ready()) {
    destination.clear();
    return VirtualAsioSharedQueueStatus::NotReady;
  }
  if (!matches(destination)) {
    destination.clear();
    return VirtualAsioSharedQueueStatus::FormatMismatch;
  }
  const auto read = atomic_load(control_->read_position);
  if (read == atomic_load(control_->write_position)) {
    destination.clear();
    atomic_increment(control_->underrun_blocks);
    return VirtualAsioSharedQueueStatus::Empty;
  }

  auto* block = slot(read);
  const auto* block_header =
      reinterpret_cast<const VirtualAsioSharedBlockHeader*>(block);
  if (block_header->connection_generation != generation_) {
    destination.clear();
    consume_invalid(read);
    return VirtualAsioSharedQueueStatus::StaleGeneration;
  }
  if (block_header->valid_frames != frames_ || block_header->reserved[0] != 0 ||
      block_header->reserved[1] != 0 || block_header->reserved[2] != 0) {
    destination.clear();
    consume_invalid(read);
    return VirtualAsioSharedQueueStatus::CorruptSlot;
  }

  const auto* samples = reinterpret_cast<const float*>(
      block + sizeof(VirtualAsioSharedBlockHeader));
  for (std::uint32_t channel = 0; channel < channels(); ++channel) {
    auto destination_samples = destination.channel(channel);
    std::memcpy(destination_samples.data(),
                samples + static_cast<std::size_t>(channel) * frames_,
                static_cast<std::size_t>(frames_) * sizeof(float));
  }
  metadata = {
      .sequence = block_header->sequence,
      .connection_generation = block_header->connection_generation,
      .sample_position = block_header->sample_position,
      .qpc_position_100ns = block_header->qpc_position_100ns,
      .flags = block_header->flags,
  };
  if (has_expected_sequence_ && metadata.sequence != expected_sequence_) {
    atomic_increment(control_->sequence_discontinuities);
  }
  expected_sequence_ = metadata.sequence + 1;
  has_expected_sequence_ = true;
  atomic_store(control_->read_position, read + 1);
  atomic_increment(control_->consumed_blocks);
  return VirtualAsioSharedQueueStatus::Completed;
}

VirtualAsioSharedQueueStats WindowsVirtualAsioSharedQueue::stats()
    const noexcept {
  return {
      .write_position = atomic_load(control_->write_position),
      .read_position = atomic_load(control_->read_position),
      .produced_blocks = atomic_load(control_->produced_blocks),
      .consumed_blocks = atomic_load(control_->consumed_blocks),
      .overrun_blocks = atomic_load(control_->overrun_blocks),
      .underrun_blocks = atomic_load(control_->underrun_blocks),
      .sequence_discontinuities =
          atomic_load(control_->sequence_discontinuities),
  };
}

bool WindowsVirtualAsioSharedQueue::ready() const noexcept {
  auto* state = reinterpret_cast<volatile LONG*>(&header_->state);
  return InterlockedCompareExchange(state, 0, 0) ==
         static_cast<LONG>(VirtualAsioSharedMemoryState::Ready);
}

bool WindowsVirtualAsioSharedQueue::matches(
    const realtime::AudioBuffer& buffer) const noexcept {
  return buffer.channels() == channels() && buffer.frames() == frames_;
}

std::byte* WindowsVirtualAsioSharedQueue::slot(
    std::uint64_t position) const noexcept {
  return slots_ + (position % capacity_) * layout_.slot_stride;
}

void WindowsVirtualAsioSharedQueue::consume_invalid(
    std::uint64_t read_position) noexcept {
  atomic_increment(control_->sequence_discontinuities);
  atomic_store(control_->read_position, read_position + 1);
  atomic_increment(control_->consumed_blocks);
  has_expected_sequence_ = false;
}

WindowsVirtualAsioSharedQueue::WindowsVirtualAsioSharedQueue(
    VirtualAsioSharedMemoryHeader* header,
    VirtualAsioSharedQueueControl* control,
    std::byte* slots,
    VirtualAsioSharedQueueLayout layout,
    std::uint32_t frames,
    std::uint32_t capacity,
    std::uint64_t generation) noexcept
    : header_(header),
      control_(control),
      slots_(slots),
      layout_(layout),
      frames_(frames),
      capacity_(capacity),
      generation_(generation) {}

const char* virtual_asio_shared_queue_status_name(
    VirtualAsioSharedQueueStatus status) noexcept {
  switch (status) {
    case VirtualAsioSharedQueueStatus::Completed:
      return "completed";
    case VirtualAsioSharedQueueStatus::NotReady:
      return "not-ready";
    case VirtualAsioSharedQueueStatus::FormatMismatch:
      return "format-mismatch";
    case VirtualAsioSharedQueueStatus::Full:
      return "full";
    case VirtualAsioSharedQueueStatus::Empty:
      return "empty";
    case VirtualAsioSharedQueueStatus::StaleGeneration:
      return "stale-generation";
    case VirtualAsioSharedQueueStatus::CorruptSlot:
      return "corrupt-slot";
  }
  return "unknown";
}

}  // namespace sar::platform
