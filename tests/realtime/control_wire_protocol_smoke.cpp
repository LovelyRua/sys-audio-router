#include "core/control/control_wire_protocol.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input-1", "Input 1"});
  preset.matrix.outputs.push_back({"output-1", "Output 1"});
  preset.matrix.routes.push_back({"input-1", "output-1", 0.5F, false});
  return preset;
}

void write_u16(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value & 0xFFU);
  bytes[offset + 1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

}  // namespace

int main() {
  sar::control::ControlCommand command;
  command.command_id = "gain-1";
  command.type = sar::control::ControlCommandType::SetGain;
  command.input_id = "input-1";
  command.output_id = "output-1";
  command.gain = 0.25F;
  command.preset = make_preset();
  command.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiMatrix;
  command.audio_runtime.endpoints = {
      {"capture-1", "native-capture-1",
       sar::control::AudioRuntimeEndpointDirection::Capture, false, 0, 2},
      {"render-1", "native-render-1",
       sar::control::AudioRuntimeEndpointDirection::Render, true, 0, 2},
  };
  command.audio_runtime.endpoints[0].backend =
      sar::control::AudioRuntimeEndpointBackend::PhysicalAsio;
  command.audio_runtime.endpoints[0].device_group_id = "studio-asio";
  command.audio_runtime.endpoints[0].sample_rate = 48000;
  command.audio_runtime.endpoints[0].block_frames = 128;
  command.virtual_asio_devices = {{
      .device_id = "main",
      .clsid = "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}",
      .registry_name = "System Audio Route Main",
      .broker_token = "main",
      .input_channels = 8,
      .output_channels = 8,
      .enabled = true,
  }};

  const auto encoded_command = sar::control::encode_control_command(command);
  assert(encoded_command.ok());
  const auto decoded_command =
      sar::control::decode_control_command(encoded_command.bytes);
  assert(decoded_command.ok());
  assert(decoded_command.command.command_id == command.command_id);
  assert(decoded_command.command.type == command.type);
  assert(decoded_command.command.input_id == command.input_id);
  assert(decoded_command.command.output_id == command.output_id);
  assert(decoded_command.command.gain == command.gain);
  assert(decoded_command.command.preset.matrix.routes.size() == 1);
  assert(decoded_command.command.audio_runtime.mode ==
         sar::control::AudioRuntimeMode::WasapiMatrix);
  assert(decoded_command.command.audio_runtime.endpoints.size() == 2);
  assert(decoded_command.command.audio_runtime.endpoints[0].endpoint_id ==
         "capture-1");
  assert(decoded_command.command.audio_runtime.endpoints[0].device_id ==
         "native-capture-1");
  assert(decoded_command.command.audio_runtime.endpoints[0].backend ==
         sar::control::AudioRuntimeEndpointBackend::PhysicalAsio);
  assert(decoded_command.command.audio_runtime.endpoints[0].device_group_id ==
         "studio-asio");
  assert(decoded_command.command.audio_runtime.endpoints[0].sample_rate ==
         48000);
  assert(decoded_command.command.audio_runtime.endpoints[0].block_frames ==
         128);
  assert(decoded_command.command.audio_runtime.endpoints[1].clock_master);
  assert(decoded_command.command.virtual_asio_devices.size() == 1);
  assert(decoded_command.command.virtual_asio_devices[0].input_channels == 8);

  auto legacy_command = command;
  legacy_command.audio_runtime = {};
  legacy_command.audio_runtime.mode =
      sar::control::AudioRuntimeMode::WasapiDuplex;
  legacy_command.audio_runtime.capture_device_id = "legacy-capture";
  legacy_command.audio_runtime.render_device_id = "legacy-render";
  legacy_command.virtual_asio_devices.clear();
  auto encoded_v8 = sar::control::encode_control_command(legacy_command).bytes;
  // v8 has no endpoint count, Physical ASIO payload, Virtual ASIO count, or
  // endpoint extension count.
  encoded_v8.resize(encoded_v8.size() - 8 * sizeof(std::uint32_t));
  write_u16(encoded_v8, 4, 8);
  write_u32(encoded_v8, 8,
            static_cast<std::uint32_t>(encoded_v8.size() -
                                       sar::control::kControlWireHeaderSize));
  const auto decoded_v8 = sar::control::decode_control_command(encoded_v8);
  assert(decoded_v8.ok());
  assert(decoded_v8.command.audio_runtime.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(decoded_v8.command.audio_runtime.endpoints.empty());

  auto encoded_v13 = sar::control::encode_control_command(command).bytes;
  const std::size_t command_extension_bytes =
      sizeof(std::uint32_t) +
      command.audio_runtime.endpoints.size() * 4 * sizeof(std::uint32_t) +
      command.audio_runtime.endpoints[0].device_group_id.size();
  encoded_v13.resize(encoded_v13.size() - command_extension_bytes);
  write_u16(encoded_v13, 4, 13);
  write_u32(encoded_v13, 8,
            static_cast<std::uint32_t>(encoded_v13.size() -
                                       sar::control::kControlWireHeaderSize));
  const auto decoded_v13 = sar::control::decode_control_command(encoded_v13);
  assert(decoded_v13.ok());
  assert(decoded_v13.command.audio_runtime.endpoints.size() == 2);
  assert(decoded_v13.command.audio_runtime.endpoints[0].backend ==
         sar::control::AudioRuntimeEndpointBackend::Wasapi);
  assert(decoded_v13.command.audio_runtime.endpoints[0].device_group_id.empty());
  assert(decoded_v13.command.audio_runtime.endpoints[0].sample_rate == 0);
  assert(decoded_v13.command.audio_runtime.endpoints[0].block_frames == 0);

  auto physical_command = command;
  physical_command.audio_runtime = {};
  physical_command.audio_runtime.mode =
      sar::control::AudioRuntimeMode::PhysicalAsio;
  physical_command.audio_runtime.physical_asio_driver_clsid =
      "{12345678-1234-1234-1234-1234567890AB}";
  physical_command.audio_runtime.physical_asio_sample_rate = 96000;
  physical_command.audio_runtime.physical_asio_block_frames = 256;
  physical_command.audio_runtime.physical_asio_input_channels = {0, 2};
  physical_command.audio_runtime.physical_asio_output_channels = {1, 3};
  const auto encoded_physical =
      sar::control::encode_control_command(physical_command);
  assert(encoded_physical.ok());
  const auto decoded_physical =
      sar::control::decode_control_command(encoded_physical.bytes);
  assert(decoded_physical.ok());
  assert(decoded_physical.command.audio_runtime.mode ==
         sar::control::AudioRuntimeMode::PhysicalAsio);
  assert(decoded_physical.command.audio_runtime.physical_asio_driver_clsid ==
         physical_command.audio_runtime.physical_asio_driver_clsid);
  assert(decoded_physical.command.audio_runtime.physical_asio_sample_rate ==
         96000);
  assert(decoded_physical.command.audio_runtime.physical_asio_block_frames ==
         256);
  assert(decoded_physical.command.audio_runtime.physical_asio_input_channels ==
         std::vector<std::uint32_t>({0, 2}));
  assert(decoded_physical.command.audio_runtime.physical_asio_output_channels ==
         std::vector<std::uint32_t>({1, 3}));

  sar::control::ControlResponse response;
  response.command_id = command.command_id;
  response.status = sar::control::ControlResponseStatus::Accepted;
  response.preset = make_preset();
  response.has_preset = true;
  response.has_session_state = true;
  response.next_graph_version = 7;
  response.has_active_graph = true;
  response.active_graph.version = 6;
  response.active_graph.sample_rate = 48000;
  response.active_graph.channels = 1;
  response.active_graph.frames = 128;
  response.active_graph.nodes.push_back({"matrix", "Main Matrix"});
  response.has_diagnostics = true;
  response.diagnostics.graph_version = 6;
  response.diagnostics.processed_blocks = 123;
  response.diagnostics.virtual_asio_pushed_blocks = 456;
  response.diagnostics.virtual_asio_producer_underflows = 12;
  response.diagnostics.virtual_asio_producer_overflows = 3;
  response.diagnostics.virtual_asio_peak = 0.75;
  response.has_wasapi_recovery = true;
  response.wasapi_recovery.state = sar::control::WasapiRecoveryState::Backoff;
  response.wasapi_recovery.runtime_health =
      sar::control::WasapiRuntimeHealth::Degraded;
  response.wasapi_recovery.runtime_reason_code = "capture_discontinuity";
  response.wasapi_recovery.recovery_episode_count = 9;
  response.wasapi_recovery.successful_recovery_count = 7;
  response.wasapi_recovery.failed_recovery_count = 2;
  response.wasapi_recovery.last_recovery_duration_ms = 125;
  response.wasapi_recovery.maximum_recovery_duration_ms = 850;
  response.wasapi_recovery.endpoint_notification_reopen_count = 4;
  response.wasapi_recovery.endpoint_notification_reset_failure_count = 1;
  response.wasapi_recovery.endpoint_notification_reopen_pending = true;
  response.wasapi_recovery.wait_timeout_cycles = 11;
  response.wasapi_recovery.capture_discontinuity_cycles = 12;
  response.wasapi_recovery.render_fifo_underflow_frames = 2048;
  response.wasapi_recovery.maximum_render_recovery_silence_frames = 1024;
  response.wasapi_recovery.maximum_consecutive_capture_rate_clamped_frames = 512;
  sar::control::AudioEndpointRuntimeDiagnostics endpoint_diagnostics;
  endpoint_diagnostics.endpoint_id = "capture-a";
  endpoint_diagnostics.role =
      sar::control::AudioEndpointRuntimeRole::Follower;
  endpoint_diagnostics.diagnostics.xrun_count = 4;
  endpoint_diagnostics.diagnostics.render_fifo_underflow_cycles = 2;
  endpoint_diagnostics.recovery = response.wasapi_recovery;
  endpoint_diagnostics.queue_fill_frames = 384;
  endpoint_diagnostics.correction_ppm = -17.25;
  response.endpoint_diagnostics.push_back(endpoint_diagnostics);
  response.has_audio_runtime_state = true;
  response.audio_runtime.installed = true;
  response.audio_runtime.running = true;
  response.audio_runtime.graph_version = 6;
  response.audio_runtime.configured = true;
  response.audio_runtime.configuration = command.audio_runtime;
  response.has_devices = true;
  response.devices.push_back({
      "virtual-asio-1",
      "Virtual ASIO 1",
      sar::platform::AudioBackendKind::VirtualAsio,
      sar::platform::AudioDeviceDirection::Duplex,
      {{48000, 2, 128, 32, 32, sar::platform::AudioSampleFormat::IeeeFloat}},
      false,
      true,
      6,
      4,
  });
  response.has_virtual_asio_devices = true;
  response.virtual_asio_devices = command.virtual_asio_devices;

  const auto encoded_response = sar::control::encode_control_response(response);
  assert(encoded_response.ok());
  const auto decoded_response =
      sar::control::decode_control_response(encoded_response.bytes);
  assert(decoded_response.ok());
  assert(decoded_response.response.has_session_state);
  assert(decoded_response.response.next_graph_version == 7);
  assert(decoded_response.response.active_graph.nodes.size() == 1);
  assert(decoded_response.response.devices.size() == 1);
  assert(decoded_response.response.devices[0].is_virtual);
  assert(decoded_response.response.devices[0].input_channels == 6);
  assert(decoded_response.response.devices[0].output_channels == 4);
  assert(decoded_response.response.has_virtual_asio_devices);
  assert(decoded_response.response.virtual_asio_devices.size() == 1);
  assert(decoded_response.response.virtual_asio_devices[0].broker_token ==
         "main");
  assert(decoded_response.response.diagnostics.processed_blocks == 123);
  assert(decoded_response.response.diagnostics.virtual_asio_pushed_blocks == 456);
  assert(decoded_response.response.diagnostics.virtual_asio_producer_underflows ==
         12);
  assert(decoded_response.response.diagnostics.virtual_asio_producer_overflows ==
         3);
  assert(decoded_response.response.diagnostics.virtual_asio_peak == 0.75);
  assert(decoded_response.response.has_wasapi_recovery);
  assert(decoded_response.response.wasapi_recovery.state ==
         sar::control::WasapiRecoveryState::Backoff);
  assert(decoded_response.response.wasapi_recovery.runtime_health ==
         sar::control::WasapiRuntimeHealth::Degraded);
  assert(decoded_response.response.wasapi_recovery.runtime_reason_code ==
         "capture_discontinuity");
  assert(decoded_response.response.wasapi_recovery.recovery_episode_count == 9);
  assert(decoded_response.response.wasapi_recovery.successful_recovery_count == 7);
  assert(decoded_response.response.wasapi_recovery.failed_recovery_count == 2);
  assert(decoded_response.response.wasapi_recovery.last_recovery_duration_ms ==
         125);
  assert(decoded_response.response.wasapi_recovery.maximum_recovery_duration_ms ==
         850);
  assert(decoded_response.response.wasapi_recovery
             .endpoint_notification_reopen_count == 4);
  assert(decoded_response.response.wasapi_recovery
             .endpoint_notification_reset_failure_count == 1);
  assert(decoded_response.response.wasapi_recovery
             .endpoint_notification_reopen_pending);
  assert(decoded_response.response.wasapi_recovery.wait_timeout_cycles == 11);
  assert(decoded_response.response.wasapi_recovery.capture_discontinuity_cycles ==
         12);
  assert(decoded_response.response.wasapi_recovery.render_fifo_underflow_frames ==
         2048);
  assert(decoded_response.response.wasapi_recovery
             .maximum_render_recovery_silence_frames == 1024);
  assert(decoded_response.response.wasapi_recovery
             .maximum_consecutive_capture_rate_clamped_frames == 512);
  assert(decoded_response.response.endpoint_diagnostics.size() == 1);
  const auto& decoded_endpoint =
      decoded_response.response.endpoint_diagnostics[0];
  assert(decoded_endpoint.endpoint_id == "capture-a");
  assert(decoded_endpoint.role ==
         sar::control::AudioEndpointRuntimeRole::Follower);
  assert(decoded_endpoint.diagnostics.xrun_count == 4);
  assert(decoded_endpoint.diagnostics.render_fifo_underflow_cycles == 2);
  assert(decoded_endpoint.recovery.has_value());
  assert(decoded_endpoint.recovery->state ==
         sar::control::WasapiRecoveryState::Backoff);
  assert(decoded_endpoint.queue_fill_frames == 384);
  assert(decoded_endpoint.correction_ppm == -17.25);
  assert(decoded_response.response.has_audio_runtime_state);
  assert(decoded_response.response.audio_runtime.installed);
  assert(decoded_response.response.audio_runtime.running);
  assert(decoded_response.response.audio_runtime.graph_version == 6);
  assert(decoded_response.response.audio_runtime.configured);
  assert(decoded_response.response.audio_runtime.configuration.mode ==
         sar::control::AudioRuntimeMode::WasapiMatrix);
  assert(decoded_response.response.audio_runtime.configuration.endpoints.size() ==
         2);
  assert(decoded_response.response.audio_runtime.configuration.endpoints[1]
             .direction ==
         sar::control::AudioRuntimeEndpointDirection::Render);
  assert(decoded_response.response.audio_runtime.configuration.endpoints[0]
             .backend ==
         sar::control::AudioRuntimeEndpointBackend::PhysicalAsio);
  assert(decoded_response.response.audio_runtime.configuration.endpoints[0]
             .device_group_id == "studio-asio");

  auto encoded_response_v13 = encoded_response.bytes;
  encoded_response_v13.resize(encoded_response_v13.size() -
                              command_extension_bytes);
  write_u16(encoded_response_v13, 4, 13);
  write_u32(encoded_response_v13, 8,
            static_cast<std::uint32_t>(encoded_response_v13.size() -
                                       sar::control::kControlWireHeaderSize));
  const auto decoded_response_v13 =
      sar::control::decode_control_response(encoded_response_v13);
  assert(decoded_response_v13.ok());
  assert(decoded_response_v13.response.audio_runtime.configuration
             .endpoints[0].backend ==
         sar::control::AudioRuntimeEndpointBackend::Wasapi);

  auto truncated = encoded_command.bytes;
  truncated.pop_back();
  assert(!sar::control::decode_control_command(truncated).ok());

  auto bad_magic = encoded_command.bytes;
  bad_magic[0] ^= 0xFFU;
  const auto bad_magic_result = sar::control::decode_control_command(bad_magic);
  assert(!bad_magic_result.ok());
  assert(bad_magic_result.error.code ==
         sar::control::ControlWireErrorCode::InvalidMagic);

  auto trailing = encoded_command.bytes;
  trailing.push_back(0);
  assert(!sar::control::decode_control_command(trailing).ok());

  std::vector<std::uint8_t> oversized(
      sar::control::kControlWireMaxMessageBytes + 1);
  const auto oversized_result = sar::control::decode_control_command(oversized);
  assert(!oversized_result.ok());
  assert(oversized_result.error.code ==
         sar::control::ControlWireErrorCode::MessageTooLarge);
}
