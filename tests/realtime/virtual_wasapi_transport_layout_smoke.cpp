#include "core/platform/virtual_wasapi_transport_layout.h"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
  using sar::platform::VirtualWasapiLayoutError;
  using sar::platform::VirtualWasapiTransportConfig;
  using sar::platform::calculate_virtual_wasapi_transport_layout;
  using sar::platform::validate_virtual_wasapi_transport_layout;

  VirtualWasapiTransportConfig config;
  const auto layout = calculate_virtual_wasapi_transport_layout(config);
  assert(layout.ok());
  assert(layout.header.total_size % SAR_VWASAPI_TRANSPORT_ALIGNMENT == 0);
  assert(layout.header.ring_state_offset % SAR_VWASAPI_TRANSPORT_ALIGNMENT == 0);
  assert(layout.header.slot_table_offset % SAR_VWASAPI_TRANSPORT_ALIGNMENT == 0);
  assert(layout.header.audio_data_offset % SAR_VWASAPI_TRANSPORT_ALIGNMENT == 0);
  assert(validate_virtual_wasapi_transport_layout(&layout.header, layout.header.total_size) ==
         VirtualWasapiLayoutError::None);
  assert(validate_virtual_wasapi_transport_layout(
             &layout.header, layout.header.total_size, SAR_VWASAPI_DIRECTION_RENDER) ==
         VirtualWasapiLayoutError::None);
  assert(validate_virtual_wasapi_transport_layout(
             &layout.header, layout.header.total_size, SAR_VWASAPI_DIRECTION_CAPTURE) ==
         VirtualWasapiLayoutError::InvalidDirection);

  assert(validate_virtual_wasapi_transport_layout(nullptr, 0) ==
         VirtualWasapiLayoutError::NullHeader);
  assert(validate_virtual_wasapi_transport_layout(&layout.header, sizeof(layout.header) - 1) ==
         VirtualWasapiLayoutError::UnsupportedVersion);

  auto malformed = layout.header;
  malformed.magic ^= 1;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::UnsupportedVersion);
  malformed = layout.header;
  ++malformed.version;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::UnsupportedVersion);
  malformed = layout.header;
  malformed.header_size = sizeof(malformed) - 1;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::UnsupportedVersion);
  malformed = layout.header;
  malformed.total_size = layout.header.total_size + SAR_VWASAPI_TRANSPORT_ALIGNMENT;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::SizeMismatch);
  malformed = layout.header;
  malformed.reserved[0] = 1;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::ReservedFieldNotZero);
  malformed = layout.header;
  ++malformed.slot_table_offset;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::MisalignedOffset);
  malformed = layout.header;
  malformed.audio_data_offset = malformed.slot_table_offset;
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::OverlappingRegion);
  malformed = layout.header;
  malformed.audio_slot_stride = std::numeric_limits<std::uint32_t>::max();
  assert(validate_virtual_wasapi_transport_layout(&malformed, layout.header.total_size) ==
         VirtualWasapiLayoutError::MisalignedOffset);

  config.direction = 99;
  assert(calculate_virtual_wasapi_transport_layout(config).error ==
         VirtualWasapiLayoutError::InvalidDirection);
  config = {};
  config.sample_format = SAR_VWASAPI_SAMPLE_IEEE_FLOAT;
  config.bits_per_sample = 24;
  assert(calculate_virtual_wasapi_transport_layout(config).error ==
         VirtualWasapiLayoutError::InvalidFormat);
  config = {};
  config.channel_count = SAR_VWASAPI_TRANSPORT_MAX_CHANNELS + 1;
  assert(calculate_virtual_wasapi_transport_layout(config).error ==
         VirtualWasapiLayoutError::InvalidRange);
  config = {};
  config.slot_count = SAR_VWASAPI_TRANSPORT_MAX_SLOTS;
  config.channel_count = SAR_VWASAPI_TRANSPORT_MAX_CHANNELS;
  config.frames_per_slot = SAR_VWASAPI_TRANSPORT_MAX_FRAMES_PER_SLOT;
  assert(calculate_virtual_wasapi_transport_layout(config).error ==
         VirtualWasapiLayoutError::ArithmeticOverflow);

  config = {};
  config.direction = SAR_VWASAPI_DIRECTION_CAPTURE;
  config.sample_format = SAR_VWASAPI_SAMPLE_PCM_INT;
  config.bits_per_sample = 24;
  config.valid_bits_per_sample = 24;
  const auto capture = calculate_virtual_wasapi_transport_layout(config);
  assert(capture.ok());
  assert(validate_virtual_wasapi_transport_layout(
             &capture.header, capture.header.total_size, SAR_VWASAPI_DIRECTION_CAPTURE) ==
         VirtualWasapiLayoutError::None);

  return 0;
}
