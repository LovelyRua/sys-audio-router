#include "core/platform/virtual_asio_shared_memory_layout.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

sar::platform::VirtualAsioSharedMemoryConfig config(
    std::uint32_t inputs = 2,
    std::uint32_t outputs = 2,
    std::uint32_t frames = 128,
    std::uint32_t capacity = 8) {
  return {
      .format = {48000, frames, inputs, outputs},
      .queue_capacity_blocks = capacity,
  };
}

sar::platform::VirtualAsioSharedMemoryIdentity identity(
    std::uint64_t generation = 9) {
  return {
      .connection_generation = generation,
      .owner_process_id = 100,
      .client_process_id = 200,
      .server_nonce_low = 0x1111,
      .server_nonce_high = 0x2222,
      .client_nonce_low = 0x3333,
      .client_nonce_high = 0x4444,
  };
}

void expect_error(
    const sar::platform::VirtualAsioSharedMemoryLayoutResult& result,
    const std::string& code) {
  assert(!result.ok());
  assert(result.error().code != nullptr);
  assert(result.error().code == code);
}

}  // namespace

int main() {
  using namespace sar::platform;

  const auto stereo = calculate_virtual_asio_shared_memory_layout(
      config(), identity());
  assert(stereo.ok());
  const auto& layout = stereo.layout();
  assert(layout.header.magic == kVirtualAsioSharedMemoryMagic);
  assert(layout.header.protocol_major == 1);
  assert(layout.header.protocol_minor == 0);
  assert(layout.header.endian_tag == kVirtualAsioSharedMemoryEndianTag);
  assert(layout.header.header_bytes == 256);
  assert(layout.header.connection_generation == 9);
  assert(layout.input_queue.present());
  assert(layout.output_queue.present());
  assert(layout.input_queue.control_offset == 256);
  assert(layout.input_queue.slots_offset == 384);
  assert(layout.input_queue.sample_count == 256);
  assert(layout.input_queue.slot_stride == 1088);
  assert(layout.output_queue.control_offset == layout.input_queue.end_offset);
  assert(layout.header.total_bytes % kVirtualAsioSharedMemoryAlignment == 0);
  assert(validate_virtual_asio_shared_memory_header(
             layout.header, layout.header.total_bytes)
             .ok());

  const auto output_only = calculate_virtual_asio_shared_memory_layout(
      config(0, 2), identity(1));
  assert(output_only.ok());
  assert(!output_only.layout().input_queue.present());
  assert(output_only.layout().header.input_control_offset == 0);
  assert(output_only.layout().output_queue.control_offset == 256);

  const auto input_only = calculate_virtual_asio_shared_memory_layout(
      config(2, 0), identity(2));
  assert(input_only.ok());
  assert(input_only.layout().input_queue.present());
  assert(!input_only.layout().output_queue.present());
  assert(input_only.layout().header.output_control_offset == 0);

  expect_error(calculate_virtual_asio_shared_memory_layout(
                   config(0, 0), identity(1)),
               "invalid_virtual_asio_shared_format");
  expect_error(calculate_virtual_asio_shared_memory_layout(
                   config(2, 2, 0), identity(1)),
               "invalid_virtual_asio_shared_format");
  expect_error(calculate_virtual_asio_shared_memory_layout(
                   config(2, 2, 128, 0), identity(1)),
               "invalid_virtual_asio_queue_capacity");
  expect_error(calculate_virtual_asio_shared_memory_layout(
                   config(), identity(0)),
               "invalid_virtual_asio_connection_identity");
  expect_error(calculate_virtual_asio_shared_memory_layout(
                   config(kVirtualAsioMaxChannels,
                          kVirtualAsioMaxChannels,
                          kVirtualAsioMaxFramesPerBlock,
                          kVirtualAsioMaxQueueCapacityBlocks),
                   identity(1)),
               "virtual_asio_mapping_too_large");

  auto corrupted = layout.header;
  corrupted.magic = 0;
  expect_error(validate_virtual_asio_shared_memory_header(
                   corrupted, layout.header.total_bytes),
               "invalid_virtual_asio_shared_header");
  corrupted = layout.header;
  corrupted.protocol_major = 2;
  expect_error(validate_virtual_asio_shared_memory_header(
                   corrupted, layout.header.total_bytes),
               "unsupported_virtual_asio_shared_protocol");
  corrupted = layout.header;
  corrupted.output_slots_offset += 64;
  expect_error(validate_virtual_asio_shared_memory_header(
                   corrupted, layout.header.total_bytes),
               "invalid_virtual_asio_shared_header");
  expect_error(validate_virtual_asio_shared_memory_header(
                   layout.header, layout.header.total_bytes - 1),
               "virtual_asio_mapping_truncated");
}
