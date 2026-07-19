#include "core/platform/virtual_asio_shared_memory_layout.h"

#include <limits>
#include <utility>

namespace sar::platform {
namespace {

constexpr VirtualAsioSharedMemoryLayoutError kInvalidFormat{
    "invalid_virtual_asio_shared_format",
    "Virtual ASIO shared memory requires a valid fixed audio format."};
constexpr VirtualAsioSharedMemoryLayoutError kInvalidCapacity{
    "invalid_virtual_asio_queue_capacity",
    "Virtual ASIO queue capacity is outside the supported range."};
constexpr VirtualAsioSharedMemoryLayoutError kInvalidIdentity{
    "invalid_virtual_asio_connection_identity",
    "Virtual ASIO shared memory requires non-zero process IDs and generation."};
constexpr VirtualAsioSharedMemoryLayoutError kLayoutTooLarge{
    "virtual_asio_mapping_too_large",
    "Virtual ASIO shared memory layout exceeds its bounded mapping size."};
constexpr VirtualAsioSharedMemoryLayoutError kInvalidHeader{
    "invalid_virtual_asio_shared_header",
    "Virtual ASIO shared memory header does not match the calculated layout."};
constexpr VirtualAsioSharedMemoryLayoutError kUnsupportedProtocol{
    "unsupported_virtual_asio_shared_protocol",
    "Virtual ASIO shared memory protocol version is unsupported."};
constexpr VirtualAsioSharedMemoryLayoutError kMappingTruncated{
    "virtual_asio_mapping_truncated",
    "Virtual ASIO shared memory mapping is smaller than its declared layout."};

bool add_checked(std::uint64_t left,
                 std::uint64_t right,
                 std::uint64_t& result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  result = left + right;
  return true;
}

bool multiply_checked(std::uint64_t left,
                      std::uint64_t right,
                      std::uint64_t& result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  result = left * right;
  return true;
}

bool align_checked(std::uint64_t value, std::uint64_t& result) noexcept {
  constexpr auto mask =
      static_cast<std::uint64_t>(kVirtualAsioSharedMemoryAlignment - 1);
  std::uint64_t expanded = 0;
  if (!add_checked(value, mask, expanded)) {
    return false;
  }
  result = expanded & ~mask;
  return true;
}

bool build_queue(std::uint64_t start,
                 std::uint32_t channels,
                 std::uint32_t frames,
                 std::uint32_t capacity,
                 VirtualAsioSharedQueueLayout& queue) noexcept {
  queue.channels = channels;
  if (channels == 0) {
    queue.end_offset = start;
    return true;
  }

  std::uint64_t sample_count = 0;
  if (!multiply_checked(channels, frames, sample_count) ||
      sample_count > std::numeric_limits<std::uint32_t>::max()) {
    return false;
  }
  queue.sample_count = static_cast<std::uint32_t>(sample_count);
  queue.control_offset = start;
  if (!add_checked(start, sizeof(VirtualAsioSharedQueueControl),
                   queue.slots_offset)) {
    return false;
  }

  std::uint64_t sample_bytes = 0;
  std::uint64_t raw_stride = 0;
  if (!multiply_checked(sample_count, sizeof(float), sample_bytes) ||
      !add_checked(sizeof(VirtualAsioSharedBlockHeader), sample_bytes,
                   raw_stride) ||
      !align_checked(raw_stride, queue.slot_stride)) {
    return false;
  }
  std::uint64_t slots_bytes = 0;
  return multiply_checked(queue.slot_stride, capacity, slots_bytes) &&
         add_checked(queue.slots_offset, slots_bytes, queue.end_offset);
}

bool headers_equal(const VirtualAsioSharedMemoryHeader& left,
                   const VirtualAsioSharedMemoryHeader& right) noexcept {
  for (std::size_t index = 0; index < 12; ++index) {
    if (left.reserved[index] != right.reserved[index]) {
      return false;
    }
  }
  return left.magic == right.magic &&
         left.protocol_major == right.protocol_major &&
         left.protocol_minor == right.protocol_minor &&
         left.header_bytes == right.header_bytes &&
         left.endian_tag == right.endian_tag &&
         left.feature_bits == right.feature_bits &&
         left.sample_format == right.sample_format &&
         left.sample_rate == right.sample_rate &&
         left.frames_per_block == right.frames_per_block &&
         left.input_channels == right.input_channels &&
         left.output_channels == right.output_channels &&
         left.queue_capacity_blocks == right.queue_capacity_blocks &&
         left.owner_process_id == right.owner_process_id &&
         left.client_process_id == right.client_process_id &&
         left.reserved32 == right.reserved32 &&
         left.connection_generation == right.connection_generation &&
         left.server_nonce_low == right.server_nonce_low &&
         left.server_nonce_high == right.server_nonce_high &&
         left.client_nonce_low == right.client_nonce_low &&
         left.client_nonce_high == right.client_nonce_high &&
         left.input_control_offset == right.input_control_offset &&
         left.input_slots_offset == right.input_slots_offset &&
         left.input_slot_stride == right.input_slot_stride &&
         left.output_control_offset == right.output_control_offset &&
         left.output_slots_offset == right.output_slots_offset &&
         left.output_slot_stride == right.output_slot_stride &&
         left.total_bytes == right.total_bytes;
}

}  // namespace

VirtualAsioSharedMemoryLayoutResult
VirtualAsioSharedMemoryLayoutResult::success(
    VirtualAsioSharedMemoryLayout layout) {
  return {std::move(layout), {}};
}

VirtualAsioSharedMemoryLayoutResult
VirtualAsioSharedMemoryLayoutResult::failure(
    VirtualAsioSharedMemoryLayoutError error) {
  return {std::nullopt, error};
}

bool VirtualAsioSharedMemoryLayoutResult::ok() const noexcept {
  return layout_.has_value();
}

const VirtualAsioSharedMemoryLayout&
VirtualAsioSharedMemoryLayoutResult::layout() const noexcept {
  return *layout_;
}

const VirtualAsioSharedMemoryLayoutError&
VirtualAsioSharedMemoryLayoutResult::error() const noexcept {
  return error_;
}

VirtualAsioSharedMemoryLayoutResult::VirtualAsioSharedMemoryLayoutResult(
    std::optional<VirtualAsioSharedMemoryLayout> layout,
    VirtualAsioSharedMemoryLayoutError error)
    : layout_(std::move(layout)), error_(error) {}

VirtualAsioSharedMemoryLayoutResult
calculate_virtual_asio_shared_memory_layout(
    const VirtualAsioSharedMemoryConfig& config,
    const VirtualAsioSharedMemoryIdentity& identity) noexcept {
  const auto& format = config.format;
  if (format.sample_rate == 0 || format.frames_per_block == 0 ||
      format.frames_per_block > kVirtualAsioMaxFramesPerBlock ||
      (format.input_channels == 0 && format.output_channels == 0) ||
      format.input_channels > kVirtualAsioMaxChannels ||
      format.output_channels > kVirtualAsioMaxChannels) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidFormat);
  }
  if (config.queue_capacity_blocks == 0 ||
      config.queue_capacity_blocks > kVirtualAsioMaxQueueCapacityBlocks) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidCapacity);
  }
  if (identity.connection_generation == 0 || identity.owner_process_id == 0 ||
      identity.client_process_id == 0 ||
      (identity.server_nonce_low == 0 && identity.server_nonce_high == 0) ||
      (identity.client_nonce_low == 0 && identity.client_nonce_high == 0)) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidIdentity);
  }

  VirtualAsioSharedMemoryLayout layout;
  auto next = static_cast<std::uint64_t>(sizeof(VirtualAsioSharedMemoryHeader));
  if (!build_queue(next,
                   format.input_channels,
                   format.frames_per_block,
                   config.queue_capacity_blocks,
                   layout.input_queue)) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kLayoutTooLarge);
  }
  next = layout.input_queue.end_offset;
  if (!build_queue(next,
                   format.output_channels,
                   format.frames_per_block,
                   config.queue_capacity_blocks,
                   layout.output_queue)) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kLayoutTooLarge);
  }
  if (!align_checked(layout.output_queue.end_offset, next) ||
      next > kVirtualAsioMaxSharedMemoryBytes) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kLayoutTooLarge);
  }

  layout.header.sample_rate = format.sample_rate;
  layout.header.frames_per_block = format.frames_per_block;
  layout.header.input_channels = format.input_channels;
  layout.header.output_channels = format.output_channels;
  layout.header.queue_capacity_blocks = config.queue_capacity_blocks;
  layout.header.owner_process_id = identity.owner_process_id;
  layout.header.client_process_id = identity.client_process_id;
  layout.header.connection_generation = identity.connection_generation;
  layout.header.server_nonce_low = identity.server_nonce_low;
  layout.header.server_nonce_high = identity.server_nonce_high;
  layout.header.client_nonce_low = identity.client_nonce_low;
  layout.header.client_nonce_high = identity.client_nonce_high;
  layout.header.input_control_offset = layout.input_queue.control_offset;
  layout.header.input_slots_offset = layout.input_queue.slots_offset;
  layout.header.input_slot_stride = layout.input_queue.slot_stride;
  layout.header.output_control_offset = layout.output_queue.control_offset;
  layout.header.output_slots_offset = layout.output_queue.slots_offset;
  layout.header.output_slot_stride = layout.output_queue.slot_stride;
  layout.header.total_bytes = next;
  return VirtualAsioSharedMemoryLayoutResult::success(std::move(layout));
}

VirtualAsioSharedMemoryLayoutResult
validate_virtual_asio_shared_memory_header(
    const VirtualAsioSharedMemoryHeader& header,
    std::uint64_t mapped_bytes) noexcept {
  if (header.magic != kVirtualAsioSharedMemoryMagic ||
      header.header_bytes != sizeof(VirtualAsioSharedMemoryHeader) ||
      header.endian_tag != kVirtualAsioSharedMemoryEndianTag ||
      header.feature_bits != 0 || header.reserved32 != 0 ||
      header.sample_format != static_cast<std::uint32_t>(
                                  VirtualAsioSharedSampleFormat::Float32Planar)) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidHeader);
  }
  if (header.protocol_major != kVirtualAsioSharedMemoryProtocolMajor ||
      header.protocol_minor > kVirtualAsioSharedMemoryProtocolMinor) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kUnsupportedProtocol);
  }
  const auto state = static_cast<VirtualAsioSharedMemoryState>(header.state);
  if (state != VirtualAsioSharedMemoryState::Initializing &&
      state != VirtualAsioSharedMemoryState::Ready &&
      state != VirtualAsioSharedMemoryState::Stopping &&
      state != VirtualAsioSharedMemoryState::Faulted) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidHeader);
  }

  VirtualAsioSharedMemoryConfig config{
      .format = {header.sample_rate,
                 header.frames_per_block,
                 header.input_channels,
                 header.output_channels},
      .queue_capacity_blocks = header.queue_capacity_blocks,
  };
  auto calculated = calculate_virtual_asio_shared_memory_layout(
      config,
      {
          .connection_generation = header.connection_generation,
          .owner_process_id = header.owner_process_id,
          .client_process_id = header.client_process_id,
          .server_nonce_low = header.server_nonce_low,
          .server_nonce_high = header.server_nonce_high,
          .client_nonce_low = header.client_nonce_low,
          .client_nonce_high = header.client_nonce_high,
      });
  if (!calculated.ok() || !headers_equal(header, calculated.layout().header)) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kInvalidHeader);
  }
  if (header.total_bytes > mapped_bytes) {
    return VirtualAsioSharedMemoryLayoutResult::failure(kMappingTruncated);
  }
  return calculated;
}

}  // namespace sar::platform
