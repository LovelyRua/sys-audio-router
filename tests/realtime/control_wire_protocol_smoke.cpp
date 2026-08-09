#include "core/control/control_wire_protocol.h"

#include <cassert>
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

}  // namespace

int main() {
  sar::control::ControlCommand command;
  command.command_id = "gain-1";
  command.type = sar::control::ControlCommandType::SetGain;
  command.input_id = "input-1";
  command.output_id = "output-1";
  command.gain = 0.25F;
  command.preset = make_preset();
  command.audio_runtime.mode =
      sar::control::AudioRuntimeMode::WasapiDuplex;
  command.audio_runtime.capture_device_id = "capture-1";
  command.audio_runtime.render_device_id = "render-1";

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
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(decoded_command.command.audio_runtime.capture_device_id == "capture-1");
  assert(decoded_command.command.audio_runtime.render_device_id == "render-1");

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
  });

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
  assert(decoded_response.response.has_audio_runtime_state);
  assert(decoded_response.response.audio_runtime.installed);
  assert(decoded_response.response.audio_runtime.running);
  assert(decoded_response.response.audio_runtime.graph_version == 6);
  assert(decoded_response.response.audio_runtime.configured);
  assert(decoded_response.response.audio_runtime.configuration.mode ==
         sar::control::AudioRuntimeMode::WasapiDuplex);
  assert(decoded_response.response.audio_runtime.configuration.capture_device_id ==
         "capture-1");

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
