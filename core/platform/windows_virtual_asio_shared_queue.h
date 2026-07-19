#pragma once

#include "core/platform/windows_virtual_asio_shared_memory.h"
#include "core/realtime/audio_buffer.h"

#include <cstdint>
#include <optional>

namespace sar::platform {

enum class VirtualAsioSharedQueueDirection {
  Input,
  Output,
};

enum class VirtualAsioSharedQueueStatus {
  Completed,
  NotReady,
  FormatMismatch,
  Full,
  Empty,
  StaleGeneration,
  CorruptSlot,
  CorruptControl,
};

struct VirtualAsioSharedBlockMetadata {
  std::uint64_t sequence = 0;
  std::uint64_t connection_generation = 0;
  std::uint64_t sample_position = 0;
  std::uint64_t qpc_position_100ns = 0;
  std::uint32_t flags = 0;
};

struct VirtualAsioSharedQueueStats {
  std::uint64_t write_position = 0;
  std::uint64_t read_position = 0;
  std::uint64_t produced_blocks = 0;
  std::uint64_t consumed_blocks = 0;
  std::uint64_t overrun_blocks = 0;
  std::uint64_t underrun_blocks = 0;
  std::uint64_t sequence_discontinuities = 0;
};

class WindowsVirtualAsioSharedQueueBindResult;

// Non-owning SPSC view. The mapped owner/view must outlive this object. Push and
// pop perform bounded copies and interlocked operations only.
class WindowsVirtualAsioSharedQueue {
 public:
  [[nodiscard]] static WindowsVirtualAsioSharedQueueBindResult bind(
      WindowsVirtualAsioSharedMemory& mapping,
      VirtualAsioSharedQueueDirection direction) noexcept;

  [[nodiscard]] std::uint32_t channels() const noexcept;
  [[nodiscard]] std::uint32_t frames_per_block() const noexcept;
  [[nodiscard]] std::uint32_t capacity_blocks() const noexcept;
  [[nodiscard]] std::uint64_t connection_generation() const noexcept;

  [[nodiscard]] VirtualAsioSharedQueueStatus push(
      const realtime::AudioBuffer& source,
      const VirtualAsioSharedBlockMetadata& metadata) noexcept;
  [[nodiscard]] VirtualAsioSharedQueueStatus pop(
      realtime::AudioBuffer& destination,
      VirtualAsioSharedBlockMetadata& metadata) noexcept;
  [[nodiscard]] VirtualAsioSharedQueueStats stats() const noexcept;

 private:
  WindowsVirtualAsioSharedQueue(
      VirtualAsioSharedMemoryHeader* header,
      VirtualAsioSharedQueueControl* control,
      std::byte* slots,
      VirtualAsioSharedQueueLayout layout,
      std::uint32_t frames,
      std::uint32_t capacity,
      std::uint64_t generation) noexcept;

  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] bool matches(const realtime::AudioBuffer& buffer) const noexcept;
  [[nodiscard]] std::byte* slot(std::uint64_t position) const noexcept;
  void consume_invalid(std::uint64_t read_position) noexcept;

  VirtualAsioSharedMemoryHeader* header_ = nullptr;
  VirtualAsioSharedQueueControl* control_ = nullptr;
  std::byte* slots_ = nullptr;
  VirtualAsioSharedQueueLayout layout_;
  std::uint32_t frames_ = 0;
  std::uint32_t capacity_ = 0;
  std::uint64_t generation_ = 0;
  std::uint64_t expected_sequence_ = 0;
  bool has_expected_sequence_ = false;
};

class WindowsVirtualAsioSharedQueueBindResult {
 public:
  static WindowsVirtualAsioSharedQueueBindResult success(
      WindowsVirtualAsioSharedQueue queue);
  static WindowsVirtualAsioSharedQueueBindResult failure(const char* error_code);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioSharedQueue& queue() noexcept;
  [[nodiscard]] WindowsVirtualAsioSharedQueue take_queue() noexcept;
  [[nodiscard]] const char* error_code() const noexcept;

 private:
  WindowsVirtualAsioSharedQueueBindResult(
      std::optional<WindowsVirtualAsioSharedQueue> queue,
      const char* error_code);

  std::optional<WindowsVirtualAsioSharedQueue> queue_;
  const char* error_code_ = nullptr;
};

[[nodiscard]] const char* virtual_asio_shared_queue_status_name(
    VirtualAsioSharedQueueStatus status) noexcept;

}  // namespace sar::platform
