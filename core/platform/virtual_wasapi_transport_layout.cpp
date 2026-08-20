#include "core/platform/virtual_wasapi_transport_layout.h"

#include <limits>

namespace sar::platform {
namespace {

constexpr std::uint64_t kAlignment = SAR_VWASAPI_TRANSPORT_ALIGNMENT;

bool checked_multiply(std::uint64_t left, std::uint64_t right, std::uint64_t& output) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
    return false;
  }
  output = left * right;
  return true;
}

bool checked_add(std::uint64_t left, std::uint64_t right, std::uint64_t& output) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left) {
    return false;
  }
  output = left + right;
  return true;
}

bool align_up(std::uint64_t value, std::uint64_t& output) noexcept {
  std::uint64_t biased = 0;
  if (!checked_add(value, kAlignment - 1, biased)) {
    return false;
  }
  output = biased & ~(kAlignment - 1);
  return true;
}

bool supported_format(std::uint32_t format, std::uint32_t bits, std::uint32_t valid_bits) noexcept {
  if (format == SAR_VWASAPI_SAMPLE_IEEE_FLOAT) {
    return bits == 32 && valid_bits == 32;
  }
  return format == SAR_VWASAPI_SAMPLE_PCM_INT &&
         (bits == 16 || bits == 24 || bits == 32) && valid_bits > 0 && valid_bits <= bits;
}

bool aligned(std::uint32_t value) noexcept {
  return value % SAR_VWASAPI_TRANSPORT_ALIGNMENT == 0;
}

bool region_ends_at_or_before(std::uint32_t offset,
                              std::uint32_t stride,
                              std::uint32_t count,
                              std::uint32_t boundary) noexcept {
  const auto end = static_cast<std::uint64_t>(offset) +
                   static_cast<std::uint64_t>(stride) * count;
  return end <= boundary;
}

template <std::size_t Size>
bool all_zero(const std::uint32_t (&values)[Size]) noexcept {
  for (const auto value : values) {
    if (value != 0) return false;
  }
  return true;
}

}  // namespace

VirtualWasapiLayoutResult calculate_virtual_wasapi_transport_layout(
    const VirtualWasapiTransportConfig& config) noexcept {
  VirtualWasapiLayoutResult result;
  if (config.direction != SAR_VWASAPI_DIRECTION_RENDER &&
      config.direction != SAR_VWASAPI_DIRECTION_CAPTURE) {
    result.error = VirtualWasapiLayoutError::InvalidDirection;
    return result;
  }
  if (!supported_format(config.sample_format, config.bits_per_sample,
                        config.valid_bits_per_sample)) {
    result.error = VirtualWasapiLayoutError::InvalidFormat;
    return result;
  }
  if (config.sample_rate < 8000 || config.sample_rate > 768000 || config.channel_count == 0 ||
      config.channel_count > SAR_VWASAPI_TRANSPORT_MAX_CHANNELS ||
      config.frames_per_slot == 0 ||
      config.frames_per_slot > SAR_VWASAPI_TRANSPORT_MAX_FRAMES_PER_SLOT ||
      config.slot_count < SAR_VWASAPI_TRANSPORT_MIN_SLOTS ||
      config.slot_count > SAR_VWASAPI_TRANSPORT_MAX_SLOTS) {
    result.error = VirtualWasapiLayoutError::InvalidRange;
    return result;
  }

  const std::uint64_t bytes_per_sample = config.bits_per_sample / 8;
  std::uint64_t audio_slot_bytes = 0;
  std::uint64_t scratch = 0;
  if (!checked_multiply(config.channel_count, config.frames_per_slot, scratch) ||
      !checked_multiply(scratch, bytes_per_sample, scratch) || !align_up(scratch, audio_slot_bytes)) {
    result.error = VirtualWasapiLayoutError::ArithmeticOverflow;
    return result;
  }

  std::uint64_t slot_table_offset = 0;
  std::uint64_t audio_data_offset = 0;
  std::uint64_t slot_table_bytes = 0;
  std::uint64_t audio_data_bytes = 0;
  std::uint64_t total_size = 0;
  if (!align_up(sizeof(SarVirtualWasapiTransportHeader) + sizeof(SarVirtualWasapiRingState),
                slot_table_offset) ||
      !checked_multiply(sizeof(SarVirtualWasapiSlotState), config.slot_count, slot_table_bytes) ||
      !checked_add(slot_table_offset, slot_table_bytes, scratch) ||
      !align_up(scratch, audio_data_offset) ||
      !checked_multiply(audio_slot_bytes, config.slot_count, audio_data_bytes) ||
      !checked_add(audio_data_offset, audio_data_bytes, scratch) || !align_up(scratch, total_size) ||
      total_size > SAR_VWASAPI_TRANSPORT_MAX_BYTES ||
      total_size > std::numeric_limits<std::uint32_t>::max()) {
    result.error = VirtualWasapiLayoutError::ArithmeticOverflow;
    return result;
  }

  auto& header = result.header;
  header.magic = SAR_VWASAPI_TRANSPORT_MAGIC;
  header.version = SAR_VWASAPI_TRANSPORT_VERSION;
  header.header_size = sizeof(header);
  header.total_size = static_cast<std::uint32_t>(total_size);
  header.direction = config.direction;
  header.sample_format = config.sample_format;
  header.sample_rate = config.sample_rate;
  header.channel_count = config.channel_count;
  header.bits_per_sample = config.bits_per_sample;
  header.valid_bits_per_sample = config.valid_bits_per_sample;
  header.frames_per_slot = config.frames_per_slot;
  header.slot_count = config.slot_count;
  header.ring_state_offset = sizeof(header);
  header.ring_state_size = sizeof(SarVirtualWasapiRingState);
  header.slot_table_offset = static_cast<std::uint32_t>(slot_table_offset);
  header.slot_stride = sizeof(SarVirtualWasapiSlotState);
  header.audio_data_offset = static_cast<std::uint32_t>(audio_data_offset);
  header.audio_slot_stride = static_cast<std::uint32_t>(audio_slot_bytes);
  return result;
}

VirtualWasapiLayoutError validate_virtual_wasapi_transport_layout(
    const SarVirtualWasapiTransportHeader* header,
    std::size_t mapped_bytes,
    std::uint32_t expected_direction) noexcept {
  if (header == nullptr) return VirtualWasapiLayoutError::NullHeader;
  if (mapped_bytes < sizeof(*header) || header->magic != SAR_VWASAPI_TRANSPORT_MAGIC ||
      header->version != SAR_VWASAPI_TRANSPORT_VERSION ||
      header->header_size != sizeof(*header)) {
    return VirtualWasapiLayoutError::UnsupportedVersion;
  }
  if ((header->direction != SAR_VWASAPI_DIRECTION_RENDER &&
       header->direction != SAR_VWASAPI_DIRECTION_CAPTURE) ||
      (expected_direction != 0 && header->direction != expected_direction)) {
    return VirtualWasapiLayoutError::InvalidDirection;
  }
  if (!supported_format(header->sample_format, header->bits_per_sample,
                        header->valid_bits_per_sample)) {
    return VirtualWasapiLayoutError::InvalidFormat;
  }
  if (!all_zero(header->reserved)) {
    return VirtualWasapiLayoutError::ReservedFieldNotZero;
  }
  if (header->total_size > mapped_bytes || header->total_size > SAR_VWASAPI_TRANSPORT_MAX_BYTES ||
      header->total_size < sizeof(*header)) {
    return VirtualWasapiLayoutError::SizeMismatch;
  }
  VirtualWasapiTransportConfig config{header->direction,
                                      header->sample_format,
                                      header->sample_rate,
                                      header->channel_count,
                                      header->bits_per_sample,
                                      header->valid_bits_per_sample,
                                      header->frames_per_slot,
                                      header->slot_count};
  const auto expected = calculate_virtual_wasapi_transport_layout(config);
  if (!expected.ok()) return expected.error;
  if (!aligned(header->ring_state_offset) || !aligned(header->slot_table_offset) ||
      !aligned(header->audio_data_offset) || !aligned(header->slot_stride) ||
      !aligned(header->audio_slot_stride)) {
    return VirtualWasapiLayoutError::MisalignedOffset;
  }
  if (header->ring_state_size != sizeof(SarVirtualWasapiRingState) ||
      header->slot_stride != sizeof(SarVirtualWasapiSlotState) ||
      header->ring_state_offset < header->header_size ||
      !region_ends_at_or_before(header->ring_state_offset, header->ring_state_size, 1,
                               header->slot_table_offset) ||
      !region_ends_at_or_before(header->slot_table_offset, header->slot_stride,
                               header->slot_count, header->audio_data_offset) ||
      !region_ends_at_or_before(header->audio_data_offset, header->audio_slot_stride,
                               header->slot_count, header->total_size)) {
    return VirtualWasapiLayoutError::OverlappingRegion;
  }
  if (header->total_size != expected.header.total_size ||
      header->ring_state_offset != expected.header.ring_state_offset ||
      header->slot_table_offset != expected.header.slot_table_offset ||
      header->audio_data_offset != expected.header.audio_data_offset ||
      header->audio_slot_stride != expected.header.audio_slot_stride) {
    return VirtualWasapiLayoutError::SizeMismatch;
  }
  return VirtualWasapiLayoutError::None;
}

}  // namespace sar::platform
