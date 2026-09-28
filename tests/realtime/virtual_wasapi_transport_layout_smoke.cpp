// CI builds smoke tests in Release; keep their assertions executable.
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "core/platform/virtual_wasapi_transport_layout.h"
#include "core/platform/virtual_wasapi_transport_ring.h"
#include "core/platform/virtual_wasapi_ring_audio_source.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

  using sar::platform::VirtualWasapiPacket;
  using sar::platform::VirtualWasapiPacketStatus;
  using sar::platform::VirtualWasapiTransportRing;
  VirtualWasapiTransportConfig small_config;
  small_config.slot_count = 2;
  small_config.frames_per_slot = 4;
  const auto small_layout = calculate_virtual_wasapi_transport_layout(small_config);
  assert(small_layout.ok());
  alignas(SAR_VWASAPI_TRANSPORT_ALIGNMENT) std::array<std::byte, 1024> memory{};
  assert(small_layout.header.total_size <= memory.size());
  assert(!VirtualWasapiTransportRing::initialize(memory.data() + 1, memory.size() - 1,
                                                  small_layout.header));
  assert(VirtualWasapiTransportRing::initialize(memory.data(), memory.size(),
                                                small_layout.header));
  assert(!VirtualWasapiTransportRing::attach(
      memory.data(), memory.size(), SAR_VWASAPI_DIRECTION_CAPTURE));
  auto producer = VirtualWasapiTransportRing::attach(
      memory.data(), memory.size(), SAR_VWASAPI_DIRECTION_RENDER);
  auto consumer = VirtualWasapiTransportRing::attach(
      memory.data(), memory.size(), SAR_VWASAPI_DIRECTION_RENDER);
  assert(producer && consumer);

  std::array<std::byte, 32> first{};
  std::array<std::byte, 32> second{};
  std::array<std::byte, 32> received{};
  first.fill(std::byte{0x25});
  second.fill(std::byte{0x74});
  VirtualWasapiPacket packet{4, 0, 11, 22};
  VirtualWasapiPacket result{};
  assert(producer->push(first, packet) == VirtualWasapiPacketStatus::Completed);
  packet.device_position = 15;
  assert(producer->push(second, packet) == VirtualWasapiPacketStatus::Completed);
  assert(producer->push(first, packet) == VirtualWasapiPacketStatus::Full);
  assert(producer->counters().dropped_frames == 4);
  assert(consumer->pop(received, result) == VirtualWasapiPacketStatus::Completed);
  assert(received == first && result.device_position == 11 && result.qpc_position == 22);
  assert(producer->push(first, packet) == VirtualWasapiPacketStatus::Completed);
  assert(consumer->pop(received, result) == VirtualWasapiPacketStatus::Completed);
  assert(received == second && result.device_position == 15);
  assert(consumer->pop(received, result) == VirtualWasapiPacketStatus::Completed);
  assert(received == first);
  assert(consumer->pop(received, result) == VirtualWasapiPacketStatus::Empty);

  packet.flags = SAR_VWASAPI_SLOT_FLAG_SILENT;
  assert(producer->push({}, packet) == VirtualWasapiPacketStatus::Completed);
  received.fill(std::byte{0x55});
  assert(consumer->pop(received, result) == VirtualWasapiPacketStatus::Completed);
  assert(std::all_of(received.begin(), received.end(),
                     [](std::byte value) { return value == std::byte{0}; }));
  assert(result.flags == SAR_VWASAPI_SLOT_FLAG_SILENT);
  assert(consumer->counters().silence_frames == 4);
  packet.flags = 0;
  assert(producer->push({}, packet) == VirtualWasapiPacketStatus::InvalidPacket);
  assert(producer->counters().malformed_packets == 1);
  assert(producer->counters().produced_frames == 16);
  assert(consumer->counters().consumed_frames == 16);

  assert(VirtualWasapiTransportRing::initialize(memory.data(), memory.size(),
                                                small_layout.header));
  auto writer = VirtualWasapiTransportRing::attach(
      memory.data(), memory.size(), SAR_VWASAPI_DIRECTION_RENDER);
  auto reader = VirtualWasapiTransportRing::attach(
      memory.data(), memory.size(), SAR_VWASAPI_DIRECTION_RENDER);
  assert(writer && reader);
  sar::platform::VirtualWasapiRingAudioSource source(*reader);
  sar::realtime::AudioBuffer block(2, 6);
  const float first_samples[8] = {0.1F, 0.2F, 0.3F, 0.4F,
                                  0.5F, 0.6F, 0.7F, 0.8F};
  const float second_samples[8] = {0.9F, 1.0F, 0.4F, 0.3F,
                                   0.2F, 0.1F, 0.8F, 0.7F};
  std::memcpy(first.data(), first_samples, first.size());
  std::memcpy(second.data(), second_samples, second.size());
  packet = {4, 0, 0, 0};
  assert(writer->push(first, packet) == VirtualWasapiPacketStatus::Completed);
  assert(writer->push(second, packet) == VirtualWasapiPacketStatus::Completed);
  assert(source.available_frames() == 8);
  assert(source.read(block));
  assert(block.channel(0)[0] == 0.1F && block.channel(1)[0] == 0.2F);
  assert(block.channel(0)[3] == 0.7F && block.channel(1)[3] == 0.8F);
  assert(block.channel(0)[4] == 0.9F && block.channel(1)[4] == 1.0F);
  assert(source.available_frames() == 2);
  sar::realtime::AudioBuffer tail(2, 2);
  assert(source.read(tail));
  assert(tail.channel(0)[0] == 0.2F && tail.channel(1)[1] == 0.7F);
  assert(source.available_frames() == 0);
  assert(!source.read(block));
  assert(std::all_of(block.channel(0).begin(), block.channel(0).end(),
                     [](float value) { return value == 0.0F; }));

  assert(writer->push(first, packet) == VirtualWasapiPacketStatus::Completed);
  auto* corrupt = reinterpret_cast<SarVirtualWasapiSlotState*>(
      memory.data() + small_layout.header.slot_table_offset);
  corrupt->frame_count = 0;
  assert(!source.read(block));
  assert(writer->counters().malformed_packets == 1);
  assert(writer->push(second, packet) == VirtualWasapiPacketStatus::Completed);
  assert(source.read(block));
  assert(block.channel(0)[0] == 0.9F);
  assert(block.channel(0)[4] == 0.0F);
  assert(source.diagnostics().dropped_blocks == 1);

  const float invalid_samples[8] = {std::numeric_limits<float>::quiet_NaN(),
                                    0.2F, 0.3F, 0.4F, 0.5F, 0.6F, 0.7F, 0.8F};
  std::memcpy(first.data(), invalid_samples, first.size());
  assert(writer->push(first, packet) == VirtualWasapiPacketStatus::Completed);
  assert(source.read(block));
  assert(block.channel(0)[0] == 0.0F);
  assert(source.diagnostics().non_finite_samples == 1);

  return 0;
}
