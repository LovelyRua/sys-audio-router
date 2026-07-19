#pragma once

#include "core/platform/virtual_asio_client_registry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace sar::platform {

inline constexpr std::uint32_t kVirtualAsioSharedMemoryMagic = 0x4D524153;
inline constexpr std::uint32_t kVirtualAsioSharedMemoryProtocolMajor = 1;
inline constexpr std::uint32_t kVirtualAsioSharedMemoryProtocolMinor = 0;
inline constexpr std::uint32_t kVirtualAsioSharedMemoryEndianTag = 0x01020304;
inline constexpr std::size_t kVirtualAsioSharedMemoryAlignment = 64;
inline constexpr std::uint32_t kVirtualAsioSharedMemoryHeaderBytes = 256;
inline constexpr std::uint32_t kVirtualAsioMaxQueueCapacityBlocks = 64;
inline constexpr std::uint64_t kVirtualAsioMaxSharedMemoryBytes =
    256ULL * 1024ULL * 1024ULL;

enum class VirtualAsioSharedSampleFormat : std::uint32_t {
  Float32Planar = 1,
};

enum class VirtualAsioSharedMemoryState : std::uint32_t {
  Initializing = 1,
  Ready = 2,
  Stopping = 3,
  Faulted = 4,
};

struct VirtualAsioSharedMemoryConfig {
  VirtualAsioFormat format;
  std::uint32_t queue_capacity_blocks = 0;
};

struct VirtualAsioSharedMemoryIdentity {
  std::uint64_t connection_generation = 0;
  std::uint32_t owner_process_id = 0;
  std::uint32_t client_process_id = 0;
  std::uint64_t server_nonce_low = 0;
  std::uint64_t server_nonce_high = 0;
  std::uint64_t client_nonce_low = 0;
  std::uint64_t client_nonce_high = 0;
};

struct alignas(kVirtualAsioSharedMemoryAlignment)
VirtualAsioSharedMemoryHeader {
  std::uint32_t magic = kVirtualAsioSharedMemoryMagic;
  std::uint32_t protocol_major = kVirtualAsioSharedMemoryProtocolMajor;
  std::uint32_t protocol_minor = kVirtualAsioSharedMemoryProtocolMinor;
  std::uint32_t header_bytes = kVirtualAsioSharedMemoryHeaderBytes;
  std::uint32_t endian_tag = kVirtualAsioSharedMemoryEndianTag;
  std::uint32_t feature_bits = 0;
  std::uint32_t state =
      static_cast<std::uint32_t>(VirtualAsioSharedMemoryState::Initializing);
  std::uint32_t sample_format =
      static_cast<std::uint32_t>(VirtualAsioSharedSampleFormat::Float32Planar);
  std::uint32_t sample_rate = 0;
  std::uint32_t frames_per_block = 0;
  std::uint32_t input_channels = 0;
  std::uint32_t output_channels = 0;
  std::uint32_t queue_capacity_blocks = 0;
  std::uint32_t owner_process_id = 0;
  std::uint32_t client_process_id = 0;
  std::uint32_t reserved32 = 0;
  std::uint64_t connection_generation = 0;
  std::uint64_t server_nonce_low = 0;
  std::uint64_t server_nonce_high = 0;
  std::uint64_t client_nonce_low = 0;
  std::uint64_t client_nonce_high = 0;
  std::uint64_t input_control_offset = 0;
  std::uint64_t input_slots_offset = 0;
  std::uint64_t input_slot_stride = 0;
  std::uint64_t output_control_offset = 0;
  std::uint64_t output_slots_offset = 0;
  std::uint64_t output_slot_stride = 0;
  std::uint64_t total_bytes = 0;
  std::uint64_t reserved[12] = {};
};

struct alignas(kVirtualAsioSharedMemoryAlignment)
VirtualAsioSharedQueueControl {
  std::uint64_t write_position = 0;
  std::uint64_t read_position = 0;
  std::uint64_t produced_blocks = 0;
  std::uint64_t consumed_blocks = 0;
  std::uint64_t overrun_blocks = 0;
  std::uint64_t underrun_blocks = 0;
  std::uint64_t sequence_discontinuities = 0;
  std::uint64_t reserved0 = 0;
  std::uint64_t reserved[8] = {};
};

struct alignas(kVirtualAsioSharedMemoryAlignment)
VirtualAsioSharedBlockHeader {
  std::uint64_t sequence = 0;
  std::uint64_t connection_generation = 0;
  std::uint64_t sample_position = 0;
  std::uint64_t qpc_position_100ns = 0;
  std::uint32_t valid_frames = 0;
  std::uint32_t flags = 0;
  std::uint64_t reserved[3] = {};
};

static_assert(sizeof(VirtualAsioSharedMemoryHeader) == 256);
static_assert(sizeof(VirtualAsioSharedQueueControl) == 128);
static_assert(sizeof(VirtualAsioSharedBlockHeader) == 64);

struct VirtualAsioSharedQueueLayout {
  std::uint64_t control_offset = 0;
  std::uint64_t slots_offset = 0;
  std::uint64_t slot_stride = 0;
  std::uint64_t end_offset = 0;
  std::uint32_t channels = 0;
  std::uint32_t sample_count = 0;

  [[nodiscard]] bool present() const noexcept { return channels != 0; }
};

struct VirtualAsioSharedMemoryLayout {
  VirtualAsioSharedMemoryHeader header;
  VirtualAsioSharedQueueLayout input_queue;
  VirtualAsioSharedQueueLayout output_queue;
};

struct VirtualAsioSharedMemoryLayoutError {
  const char* code = nullptr;
  const char* message = nullptr;
};

class VirtualAsioSharedMemoryLayoutResult {
 public:
  static VirtualAsioSharedMemoryLayoutResult success(
      VirtualAsioSharedMemoryLayout layout);
  static VirtualAsioSharedMemoryLayoutResult failure(
      VirtualAsioSharedMemoryLayoutError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const VirtualAsioSharedMemoryLayout& layout() const noexcept;
  [[nodiscard]] const VirtualAsioSharedMemoryLayoutError& error() const noexcept;

 private:
  VirtualAsioSharedMemoryLayoutResult(
      std::optional<VirtualAsioSharedMemoryLayout> layout,
      VirtualAsioSharedMemoryLayoutError error);

  std::optional<VirtualAsioSharedMemoryLayout> layout_;
  VirtualAsioSharedMemoryLayoutError error_;
};

[[nodiscard]] VirtualAsioSharedMemoryLayoutResult
calculate_virtual_asio_shared_memory_layout(
    const VirtualAsioSharedMemoryConfig& config,
    const VirtualAsioSharedMemoryIdentity& identity) noexcept;

[[nodiscard]] VirtualAsioSharedMemoryLayoutResult
validate_virtual_asio_shared_memory_header(
    const VirtualAsioSharedMemoryHeader& header,
    std::uint64_t mapped_bytes) noexcept;

}  // namespace sar::platform
