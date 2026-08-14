#include "core/control/control_wire_protocol.h"
#include "core/control/session_file_codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

sar::control::SessionDocument make_session() {
  sar::control::SessionDocument session;
  session.preset.sample_rate = 48000;
  session.preset.frames_per_block = 64;
  session.preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  session.preset.matrix.inputs.push_back({"input", "Input"});
  session.preset.matrix.outputs.push_back({"output", "Output"});
  session.preset.matrix.routes.push_back({"input", "output", 0.5F, true});
  session.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiDuplex;
  session.audio_runtime.capture_device_id = "capture-device";
  session.audio_runtime.render_device_id = "render-device";
  session.virtual_asio_devices.push_back(
      sar::control::default_virtual_asio_device_definition());
  session.auto_start = true;
  return session;
}

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset) {
  return static_cast<std::uint32_t>(bytes[offset]) |
         (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
         (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

void write_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

std::size_t runtime_mode_offset(const std::vector<std::uint8_t>& bytes) {
  return 20 + read_u32(bytes, 16);
}

std::size_t runtime_endpoint_count_offset(
    const std::vector<std::uint8_t>& bytes) {
  auto offset = runtime_mode_offset(bytes) + 1;
  offset += 4 + read_u32(bytes, offset);
  offset += 4 + read_u32(bytes, offset);
  return offset;
}

void assert_decode_fails(const std::vector<std::uint8_t>& bytes) {
  assert(!sar::control::decode_session_file(bytes).ok());
}

}  // namespace

int main() {
  const auto session = make_session();
  const auto encoded = sar::control::encode_session_file(session);
  const auto encoded_again = sar::control::encode_session_file(session);
  assert(encoded.ok());
  assert(encoded_again.ok());
  assert(encoded.bytes() == encoded_again.bytes());
  assert(encoded.bytes().size() <= sar::control::kSessionFileMaxBytes);

  const auto decoded = sar::control::decode_session_file(encoded.bytes());
  assert(decoded.ok());
  assert(decoded.session().schema_version ==
         sar::control::kSessionDocumentSchemaVersion);
  assert(decoded.session().preset.sample_rate == 48000);
  assert(decoded.session().preset.frames_per_block == 64);
  assert(decoded.session().preset.matrix.routes.size() == 1);
  assert(decoded.session().preset.matrix.routes[0].gain == 0.5F);
  assert(decoded.session().preset.matrix.routes[0].muted);
  assert(decoded.session().audio_runtime.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(decoded.session().audio_runtime.capture_device_id == "capture-device");
  assert(decoded.session().audio_runtime.render_device_id == "render-device");
  assert(decoded.session().auto_start);
  assert(decoded.session().virtual_asio_devices.size() == 1);
  assert(decoded.session().virtual_asio_devices[0].broker_token ==
         "virtual-asio");

  auto invalid_identity = session;
  invalid_identity.virtual_asio_devices[0].broker_token = "nested\\pipe";
  assert(!sar::control::validate_session_document(invalid_identity).ok());
  auto duplicate_identity = session;
  auto duplicate_device =
      sar::control::default_virtual_asio_device_definition();
  duplicate_device.device_id = "DEFAULT";
  duplicate_device.clsid = "{83D4C47A-9834-41F4-A5EE-62BFB8F28D8A}";
  duplicate_device.registry_name = "System Audio Route Aux";
  duplicate_device.broker_token = "virtual-asio-aux";
  duplicate_identity.virtual_asio_devices.push_back(duplicate_device);
  assert(!sar::control::validate_session_document(duplicate_identity).ok());

  auto legacy_v2 = encoded.bytes();
  const auto endpoint_count_offset = runtime_endpoint_count_offset(legacy_v2);
  const auto auto_start_offset = endpoint_count_offset + 4;
  legacy_v2.resize(auto_start_offset + 1);
  legacy_v2[4] = 2;
  write_u32(legacy_v2, 12, 1);
  write_u32(legacy_v2, 8,
            static_cast<std::uint32_t>(legacy_v2.size() - 12));
  const auto decoded_v2 = sar::control::decode_session_file(legacy_v2);
  assert(decoded_v2.ok());
  assert(decoded_v2.session().virtual_asio_devices.size() == 1);

  auto legacy_v1 = legacy_v2;
  const auto legacy_preset_size = read_u32(legacy_v1, 16);
  const auto legacy_preset_trailing_field = 20 + legacy_preset_size - 1;
  legacy_v1.erase(
      legacy_v1.begin() +
      static_cast<std::ptrdiff_t>(legacy_preset_trailing_field));
  write_u32(legacy_v1, 16, legacy_preset_size - 1);
  write_u32(legacy_v1, 28, read_u32(legacy_v1, 28) - 1);
  const auto legacy_endpoint_count_offset =
      runtime_endpoint_count_offset(legacy_v1);
  legacy_v1.erase(legacy_v1.begin() +
                      static_cast<std::ptrdiff_t>(legacy_endpoint_count_offset),
                  legacy_v1.begin() +
                      static_cast<std::ptrdiff_t>(legacy_endpoint_count_offset + 4));
  legacy_v1[4] = 1;
  // Session v1 files contain the original control-wire v8 preset payload.
  legacy_v1[24] = 8;
  legacy_v1[25] = 0;
  write_u32(legacy_v1, 8,
            static_cast<std::uint32_t>(legacy_v1.size() - 12));
  const auto decoded_v1 = sar::control::decode_session_file(legacy_v1);
  assert(decoded_v1.ok());
  assert(decoded_v1.session().audio_runtime.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(decoded_v1.session().audio_runtime.endpoints.empty());

  assert_decode_fails({});

  auto truncated = encoded.bytes();
  truncated.pop_back();
  assert_decode_fails(truncated);

  auto trailing = encoded.bytes();
  trailing.push_back(0);
  write_u32(trailing, 8, read_u32(trailing, 8) + 1);
  assert_decode_fails(trailing);

  auto unknown_file_version = encoded.bytes();
  unknown_file_version[4] = 4;
  assert_decode_fails(unknown_file_version);

  auto unknown_schema_version = encoded.bytes();
  write_u32(unknown_schema_version, 12, 3);
  assert_decode_fails(unknown_schema_version);

  const auto mode_offset = runtime_mode_offset(encoded.bytes());
  auto invalid_mode = encoded.bytes();
  invalid_mode[mode_offset] = 0xFF;
  assert_decode_fails(invalid_mode);

  auto invalid_runtime_payload = encoded.bytes();
  invalid_runtime_payload[mode_offset] =
      static_cast<std::uint8_t>(sar::control::AudioRuntimeMode::None);
  assert_decode_fails(invalid_runtime_payload);

  auto invalid_bool = encoded.bytes();
  invalid_bool[auto_start_offset] = 2;
  assert_decode_fails(invalid_bool);

  auto oversized_string = encoded.bytes();
  write_u32(oversized_string,
            mode_offset + 1,
            static_cast<std::uint32_t>(
                sar::control::kControlWireMaxStringBytes + 1));
  assert_decode_fails(oversized_string);

  std::vector<std::uint8_t> oversized_file(
      sar::control::kSessionFileMaxBytes + 1, 0);
  assert_decode_fails(oversized_file);

  auto invalid_preset = session;
  invalid_preset.preset.sample_rate = 0;
  const auto invalid_preset_result =
      sar::control::encode_session_file(invalid_preset);
  assert(!invalid_preset_result.ok());
  assert(invalid_preset_result.error().code == "invalid_session");

  auto none_with_devices = session;
  none_with_devices.audio_runtime.mode = sar::control::AudioRuntimeMode::None;
  none_with_devices.auto_start = false;
  assert(!sar::control::encode_session_file(none_with_devices).ok());

  auto render = session;
  render.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiRender;
  render.audio_runtime.capture_device_id.clear();
  render.audio_runtime.render_device_id.clear();
  assert(sar::control::encode_session_file(render).ok());

  render.audio_runtime.capture_device_id = "capture-device";
  assert(!sar::control::encode_session_file(render).ok());

  auto incomplete_duplex = session;
  incomplete_duplex.audio_runtime.render_device_id.clear();
  assert(!sar::control::encode_session_file(incomplete_duplex).ok());

  auto default_duplex = session;
  default_duplex.audio_runtime.capture_device_id.clear();
  default_duplex.audio_runtime.render_device_id.clear();
  assert(sar::control::encode_session_file(default_duplex).ok());

  auto matrix = session;
  matrix.audio_runtime = {};
  matrix.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiMatrix;
  matrix.audio_runtime.endpoints = {
      {"capture-a", "native-capture-a",
       sar::control::AudioRuntimeEndpointDirection::Capture, false, 0, 2},
      {"render-main", "native-render-main",
       sar::control::AudioRuntimeEndpointDirection::Render, true, 0, 2},
      {"render-b", "native-render-b",
       sar::control::AudioRuntimeEndpointDirection::Render, false, 0, 2},
  };
  const auto encoded_matrix = sar::control::encode_session_file(matrix);
  assert(encoded_matrix.ok());
  const auto decoded_matrix =
      sar::control::decode_session_file(encoded_matrix.bytes());
  assert(decoded_matrix.ok());
  assert(decoded_matrix.session().audio_runtime.endpoints.size() == 3);
  assert(decoded_matrix.session().audio_runtime.endpoints[1].endpoint_id ==
         "render-main");
  assert(decoded_matrix.session().audio_runtime.endpoints[1].clock_master);

  auto stopped = session;
  stopped.audio_runtime.mode = sar::control::AudioRuntimeMode::None;
  stopped.audio_runtime.capture_device_id.clear();
  stopped.audio_runtime.render_device_id.clear();
  stopped.auto_start = false;
  assert(sar::control::encode_session_file(stopped).ok());
  stopped.auto_start = true;
  assert(!sar::control::encode_session_file(stopped).ok());

  auto oversized_id = session;
  oversized_id.audio_runtime.capture_device_id.assign(
      sar::control::kControlWireMaxStringBytes + 1, 'x');
  oversized_id.audio_runtime.render_device_id.assign(
      sar::control::kControlWireMaxStringBytes + 1, 'y');
  assert(!sar::control::encode_session_file(oversized_id).ok());

  auto oversized_session = session;
  oversized_session.preset.nodes.clear();
  for (std::size_t index = 0; index < 5; ++index) {
    std::string id(62000, static_cast<char>('a' + index));
    oversized_session.preset.nodes.push_back({
        std::move(id),
        std::string(62000, 'l'),
        std::string(62000, 't'),
    });
  }
  oversized_session.audio_runtime.capture_device_id.assign(
      sar::control::kControlWireMaxStringBytes, 'c');
  oversized_session.audio_runtime.render_device_id.assign(
      sar::control::kControlWireMaxStringBytes, 'r');
  const auto oversized_session_result =
      sar::control::encode_session_file(oversized_session);
  assert(!oversized_session_result.ok());
  assert(oversized_session_result.error().code == "invalid_session_file");

  auto invalid_runtime = session;
  invalid_runtime.audio_runtime.mode =
      static_cast<sar::control::AudioRuntimeMode>(99);
  assert(!sar::control::encode_session_file(invalid_runtime).ok());
}
