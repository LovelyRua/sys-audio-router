#include "core/control/control_wire_protocol.h"
#include "core/control/preset_file_codec.h"
#include "core/service/windows_named_pipe_control.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& input) {
  std::vector<std::byte> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = static_cast<std::byte>(input[index]);
  }
  return result;
}

std::vector<std::uint8_t> as_u8(std::span<const std::byte> input) {
  std::vector<std::uint8_t> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = std::to_integer<std::uint8_t>(input[index]);
  }
  return result;
}

void usage() {
  std::cerr << "Usage: sar_control_cli [--pipe NAME] state|devices|diagnostics|graph|"
               "preset-save FILE|preset-load FILE|virtual-asio-list|"
               "runtime-state|runtime-start|runtime-stop|set-gain INPUT OUTPUT "
               "VALUE|set-mute INPUT OUTPUT true|false|"
               "runtime-configure-render [RENDER_ID]|"
               "runtime-configure-duplex [CAPTURE_ID RENDER_ID]|"
               "runtime-configure-physical-asio DRIVER_CLSID SAMPLE_RATE "
               "BLOCK_FRAMES INPUT_CHANNELS OUTPUT_CHANNELS|"
               "runtime-configure-matrix (capture|render) ENDPOINT_ID "
               "(DEVICE_ID|default) FIRST_CHANNEL CHANNEL_COUNT "
               "(master|follower) [...]\n";
}

bool parse_uint32(std::string_view text, std::uint32_t& value) noexcept;

bool parse_channel_list(std::string_view text,
                        std::vector<std::uint32_t>& channels) noexcept {
  if (text == "-") {
    return true;
  }
  std::size_t begin = 0;
  while (begin < text.size()) {
    const auto end = text.find(',', begin);
    const auto item = text.substr(begin, end == std::string_view::npos
                                             ? text.size() - begin
                                             : end - begin);
    std::uint32_t channel = 0;
    if (item.empty() || !parse_uint32(item, channel)) {
      return false;
    }
    channels.push_back(channel);
    if (end == std::string_view::npos) {
      return true;
    }
    begin = end + 1;
  }
  return false;
}

bool parse_uint32(std::string_view text, std::uint32_t& value) noexcept {
  try {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(std::string{text}, &consumed, 10);
    if (consumed != text.size() ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

const char* wasapi_recovery_state_name(
    sar::control::WasapiRecoveryState state) noexcept {
  switch (state) {
    case sar::control::WasapiRecoveryState::Stopped:
      return "stopped";
    case sar::control::WasapiRecoveryState::Opening:
      return "opening";
    case sar::control::WasapiRecoveryState::Running:
      return "running";
    case sar::control::WasapiRecoveryState::Quiescing:
      return "quiescing";
    case sar::control::WasapiRecoveryState::Backoff:
      return "backoff";
    case sar::control::WasapiRecoveryState::Faulted:
      return "faulted";
  }
  return "unknown";
}

const char* wasapi_runtime_health_name(
    sar::control::WasapiRuntimeHealth health) noexcept {
  switch (health) {
    case sar::control::WasapiRuntimeHealth::Stopped:
      return "stopped";
    case sar::control::WasapiRuntimeHealth::Healthy:
      return "healthy";
    case sar::control::WasapiRuntimeHealth::Degraded:
      return "degraded";
    case sar::control::WasapiRuntimeHealth::Faulted:
      return "faulted";
  }
  return "unknown";
}

std::filesystem::path utf8_path(std::string_view value) {
  const auto* first = reinterpret_cast<const char8_t*>(value.data());
  return std::filesystem::path(std::u8string(first, first + value.size()));
}

bool read_preset_file(std::string_view file_name,
                      sar::control::PresetDocument& preset) {
  std::ifstream input(utf8_path(file_name), std::ios::binary | std::ios::ate);
  if (!input) {
    std::cerr << "preset_file_open_failed: Could not open preset file.\n";
    return false;
  }
  const auto end = input.tellg();
  if (end <= 0 ||
      static_cast<std::uintmax_t>(end) >
          sar::control::kControlWireMaxMessageBytes) {
    std::cerr << "invalid_preset_file: Preset file size is invalid.\n";
    return false;
  }

  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
  input.seekg(0, std::ios::beg);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    std::cerr << "preset_file_read_failed: Could not read preset file.\n";
    return false;
  }

  auto decoded = sar::control::decode_preset_file(bytes);
  if (!decoded.ok()) {
    std::cerr << decoded.error().code << ": " << decoded.error().message << '\n';
    return false;
  }
  preset = decoded.take_preset();
  return true;
}

bool write_preset_file_atomic(std::string_view file_name,
                              const sar::control::PresetDocument& preset) {
  auto encoded = sar::control::encode_preset_file(preset);
  if (!encoded.ok()) {
    std::cerr << encoded.error().code << ": " << encoded.error().message << '\n';
    return false;
  }

  const auto target = utf8_path(file_name);
  auto temporary = target;
  temporary += L".tmp." + std::to_wstring(GetCurrentProcessId());
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      std::cerr << "preset_file_open_failed: Could not create temporary preset file.\n";
      return false;
    }
    const auto& bytes = encoded.bytes();
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    if (!output) {
      output.close();
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      std::cerr << "preset_file_write_failed: Could not write preset file.\n";
      return false;
    }
  }

  if (!MoveFileExW(temporary.c_str(),
                   target.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto native_error = GetLastError();
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    std::cerr << "preset_file_replace_failed: Could not replace preset file (win32="
              << native_error << ").\n";
    return false;
  }
  return true;
}

const char* backend_name(sar::platform::AudioBackendKind backend) {
  switch (backend) {
    case sar::platform::AudioBackendKind::Wasapi:
      return "wasapi";
    case sar::platform::AudioBackendKind::WasapiLoopback:
      return "wasapi-loopback";
    case sar::platform::AudioBackendKind::Asio:
      return "asio";
    case sar::platform::AudioBackendKind::VirtualWasapi:
      return "virtual-wasapi";
    case sar::platform::AudioBackendKind::VirtualAsio:
      return "virtual-asio";
    case sar::platform::AudioBackendKind::Mock:
      return "mock";
  }
  return "unknown";
}

const char* direction_name(sar::platform::AudioDeviceDirection direction) {
  switch (direction) {
    case sar::platform::AudioDeviceDirection::Input:
      return "input";
    case sar::platform::AudioDeviceDirection::Output:
      return "output";
    case sar::platform::AudioDeviceDirection::Duplex:
      return "duplex";
  }
  return "unknown";
}

const char* sample_format_name(sar::platform::AudioSampleFormat format) {
  switch (format) {
    case sar::platform::AudioSampleFormat::Unknown:
      return "unknown";
    case sar::platform::AudioSampleFormat::PcmInt:
      return "pcm-int";
    case sar::platform::AudioSampleFormat::IeeeFloat:
      return "float";
  }
  return "unknown";
}

const char* runtime_mode_name(sar::control::AudioRuntimeMode mode) {
  switch (mode) {
    case sar::control::AudioRuntimeMode::None:
      return "none";
    case sar::control::AudioRuntimeMode::WasapiRender:
      return "wasapi-render";
    case sar::control::AudioRuntimeMode::WasapiDuplex:
      return "wasapi-duplex";
    case sar::control::AudioRuntimeMode::WasapiMatrix:
      return "wasapi-matrix";
    case sar::control::AudioRuntimeMode::PhysicalAsio:
      return "physical-asio";
  }
  return "unknown";
}

}  // namespace

int main(int argc, char** argv) {
  sar::service::NamedPipeControlConfig pipe_config;
  int index = 1;
  if (index + 1 < argc && std::string{argv[index]} == "--pipe") {
    const std::string name = argv[index + 1];
    pipe_config.pipe_name.assign(name.begin(), name.end());
    index += 2;
  }
  if (index >= argc) {
    usage();
    return 2;
  }

  sar::control::ControlCommand command;
  command.command_id =
      "cli-" + std::to_string(GetCurrentProcessId()) + "-" +
      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  std::string preset_file_name;
  const std::string operation = argv[index++];
  if (operation == "state") {
    command.type = sar::control::ControlCommandType::QuerySessionState;
  } else if (operation == "devices") {
    command.type = sar::control::ControlCommandType::ListDevices;
  } else if (operation == "diagnostics") {
    command.type = sar::control::ControlCommandType::QueryDiagnostics;
  } else if (operation == "graph") {
    command.type = sar::control::ControlCommandType::QueryActiveGraph;
  } else if (operation == "preset-save" && index < argc) {
    command.type = sar::control::ControlCommandType::SavePreset;
    preset_file_name = argv[index++];
  } else if (operation == "preset-load" && index < argc) {
    command.type = sar::control::ControlCommandType::LoadPreset;
    preset_file_name = argv[index++];
    if (!read_preset_file(preset_file_name, command.preset)) {
      return 1;
    }
  } else if (operation == "runtime-state") {
    command.type = sar::control::ControlCommandType::QueryAudioRuntime;
  } else if (operation == "virtual-asio-list") {
    command.type =
        sar::control::ControlCommandType::QueryVirtualAsioDevices;
  } else if (operation == "runtime-start") {
    command.type = sar::control::ControlCommandType::StartAudioRuntime;
  } else if (operation == "runtime-stop") {
    command.type = sar::control::ControlCommandType::StopAudioRuntime;
  } else if (operation == "runtime-configure-render") {
    command.type = sar::control::ControlCommandType::ConfigureAudioRuntime;
    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiRender;
    if (index < argc) {
      command.audio_runtime.render_device_id = argv[index++];
    }
  } else if (operation == "runtime-configure-duplex") {
    command.type = sar::control::ControlCommandType::ConfigureAudioRuntime;
    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiDuplex;
    if (index + 1 < argc) {
      command.audio_runtime.capture_device_id = argv[index++];
      command.audio_runtime.render_device_id = argv[index++];
    } else if (index < argc) {
      usage();
      return 2;
    }
  } else if (operation == "runtime-configure-physical-asio") {
    command.type = sar::control::ControlCommandType::ConfigureAudioRuntime;
    command.audio_runtime.mode = sar::control::AudioRuntimeMode::PhysicalAsio;
    if (index + 4 >= argc) {
      usage();
      return 2;
    }
    command.audio_runtime.physical_asio_driver_clsid = argv[index++];
    if (!parse_uint32(argv[index++],
                      command.audio_runtime.physical_asio_sample_rate) ||
        !parse_uint32(argv[index++],
                      command.audio_runtime.physical_asio_block_frames) ||
        !parse_channel_list(
            argv[index++],
            command.audio_runtime.physical_asio_input_channels) ||
        !parse_channel_list(
            argv[index++],
            command.audio_runtime.physical_asio_output_channels)) {
      usage();
      return 2;
    }
  } else if (operation == "runtime-configure-matrix") {
    command.type = sar::control::ControlCommandType::ConfigureAudioRuntime;
    command.audio_runtime.mode =
        sar::control::AudioRuntimeMode::WasapiMatrix;
    constexpr int kEndpointArgumentCount = 6;
    if (index >= argc || (argc - index) % kEndpointArgumentCount != 0) {
      usage();
      return 2;
    }
    while (index < argc) {
      const std::string direction = argv[index++];
      sar::control::AudioRuntimeEndpointConfiguration endpoint;
      if (direction == "capture") {
        endpoint.direction =
            sar::control::AudioRuntimeEndpointDirection::Capture;
      } else if (direction == "render") {
        endpoint.direction =
            sar::control::AudioRuntimeEndpointDirection::Render;
      } else {
        usage();
        return 2;
      }
      endpoint.endpoint_id = argv[index++];
      endpoint.device_id = argv[index++];
      if (endpoint.device_id == "default") {
        endpoint.device_id.clear();
      }
      if (!parse_uint32(argv[index++], endpoint.first_channel) ||
          !parse_uint32(argv[index++], endpoint.channel_count)) {
        usage();
        return 2;
      }
      const std::string clock_role = argv[index++];
      if (clock_role == "master") {
        endpoint.clock_master = true;
      } else if (clock_role != "follower") {
        usage();
        return 2;
      }
      command.audio_runtime.endpoints.push_back(std::move(endpoint));
    }
  } else if (operation == "set-gain" && index + 2 < argc) {
    command.type = sar::control::ControlCommandType::SetGain;
    command.input_id = argv[index++];
    command.output_id = argv[index++];
    try {
      command.gain = std::stof(argv[index++]);
    } catch (...) {
      usage();
      return 2;
    }
  } else if (operation == "set-mute" && index + 2 < argc) {
    command.type = sar::control::ControlCommandType::SetMute;
    command.input_id = argv[index++];
    command.output_id = argv[index++];
    const std::string value = argv[index++];
    if (value != "true" && value != "false") {
      usage();
      return 2;
    }
    command.mute = value == "true";
  } else {
    usage();
    return 2;
  }
  if (index != argc) {
    usage();
    return 2;
  }

  const auto request = sar::control::encode_control_command(command);
  if (!request.ok()) {
    std::cerr << "control_request_encode_failed\n";
    return 1;
  }
  const auto transaction = sar::service::transact_named_pipe_control(
      pipe_config, as_bytes(request.bytes));
  if (!transaction.ok()) {
    std::cerr << transaction.error().code << ": "
              << transaction.error().message << '\n';
    return 1;
  }
  const auto response =
      sar::control::decode_control_response(as_u8(transaction.payload()));
  if (!response.ok()) {
    std::cerr << "control_response_decode_failed\n";
    return 1;
  }
  if (response.response.status ==
      sar::control::ControlResponseStatus::Rejected) {
    for (const auto& error : response.response.errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }
  if (operation == "preset-save") {
    if (!response.response.has_preset) {
      std::cerr << "preset_missing: Engine response did not contain a preset.\n";
      return 1;
    }
    if (!write_preset_file_atomic(preset_file_name, response.response.preset)) {
      return 1;
    }
  }

  std::cout << "control_response status=accepted command_id="
            << response.response.command_id;
  if (response.response.has_preset) {
    std::cout << " preset_routes="
              << response.response.preset.matrix.routes.size();
  }
  if (response.response.has_active_graph) {
    std::cout << " graph_version=" << response.response.active_graph.version
              << " sample_rate=" << response.response.active_graph.sample_rate
              << " channels=" << response.response.active_graph.channels
              << " frames=" << response.response.active_graph.frames;
  }
  if (!preset_file_name.empty()) {
    std::cout << " preset_file=" << std::quoted(preset_file_name);
  }
  if (response.response.has_session_state) {
    std::cout << " next_graph_version="
              << response.response.next_graph_version
              << " routes=" << response.response.preset.matrix.routes.size();
  }
  if (response.response.has_devices) {
    std::cout << " devices=" << response.response.devices.size();
  }
  if (response.response.has_diagnostics) {
    std::cout << " processed_blocks="
              << response.response.diagnostics.processed_blocks
              << " xruns=" << response.response.diagnostics.xrun_count
              << " capture_fifo_frames="
              << response.response.diagnostics.capture_fifo_fill_frames
              << " render_fifo_frames="
              << response.response.diagnostics.render_fifo_fill_frames
              << " capture_overflow_frames="
              << response.response.diagnostics.capture_fifo_overflow_frames
              << " render_overflow_frames="
              << response.response.diagnostics.render_fifo_overflow_frames
              << " render_underflow_frames="
              << response.response.diagnostics.render_fifo_underflow_frames
              << " asio_pushed_blocks="
              << response.response.diagnostics.virtual_asio_pushed_blocks
              << " asio_dropped_blocks="
              << response.response.diagnostics.virtual_asio_dropped_blocks
              << " asio_producer_underflows="
              << response.response.diagnostics.virtual_asio_producer_underflows
              << " asio_producer_overflows="
              << response.response.diagnostics.virtual_asio_producer_overflows
              << " asio_consumed_blocks="
              << response.response.diagnostics.virtual_asio_consumed_blocks
              << " asio_mixed_blocks="
              << response.response.diagnostics.virtual_asio_mixed_blocks
              << " asio_silent_reads="
              << response.response.diagnostics.virtual_asio_silent_reads
              << " asio_clipped_samples="
              << response.response.diagnostics.virtual_asio_clipped_samples
              << " asio_non_finite_samples="
              << response.response.diagnostics.virtual_asio_non_finite_samples
              << " asio_max_queue_depth="
              << response.response.diagnostics.virtual_asio_maximum_queue_depth
              << " asio_active_producers="
              << response.response.diagnostics.virtual_asio_active_producers
              << " asio_peak="
              << response.response.diagnostics.virtual_asio_peak
              << " callback_peak_us="
              << response.response.diagnostics.peak_callback_seconds * 1'000'000.0;
  }
  if (response.response.has_wasapi_recovery) {
    const auto& wasapi = response.response.wasapi_recovery;
    std::cout << " wasapi_recovery_state="
              << wasapi_recovery_state_name(wasapi.state)
              << " wasapi_runtime_health="
              << wasapi_runtime_health_name(wasapi.runtime_health)
              << " wasapi_runtime_reason="
              << std::quoted(wasapi.runtime_reason_code)
              << " wasapi_recovery_episodes=" << wasapi.recovery_episode_count
              << " wasapi_recovery_successes="
              << wasapi.successful_recovery_count
              << " wasapi_recovery_failures=" << wasapi.failed_recovery_count
              << " wasapi_last_recovery_ms="
              << wasapi.last_recovery_duration_ms
              << " wasapi_max_recovery_ms="
              << wasapi.maximum_recovery_duration_ms
              << " wasapi_notification_reopens="
              << wasapi.endpoint_notification_reopen_count
              << " wasapi_notification_reset_failures="
              << wasapi.endpoint_notification_reset_failure_count
              << " wasapi_notification_reopen_pending="
              << (wasapi.endpoint_notification_reopen_pending ? "true" : "false")
              << " wasapi_wait_timeouts=" << wasapi.wait_timeout_cycles
              << " wasapi_capture_discontinuities="
              << wasapi.capture_discontinuity_cycles
              << " wasapi_render_underflow_frames="
              << wasapi.render_fifo_underflow_frames
              << " wasapi_max_recovery_silence_frames="
              << wasapi.maximum_render_recovery_silence_frames
              << " wasapi_max_capture_rate_clamp_frames="
              << wasapi.maximum_consecutive_capture_rate_clamped_frames;
  }
  if (response.response.has_audio_runtime_state) {
    std::cout << " runtime_installed="
              << (response.response.audio_runtime.installed ? "true" : "false")
              << " runtime_running="
              << (response.response.audio_runtime.running ? "true" : "false")
              << " runtime_graph_version="
              << response.response.audio_runtime.graph_version;
    if (response.response.audio_runtime.configured) {
      std::cout << " runtime_mode="
                << runtime_mode_name(
                       response.response.audio_runtime.configuration.mode)
                << " capture_id="
                << (response.response.audio_runtime.configuration
                            .capture_device_id.empty()
                        ? "default"
                        : response.response.audio_runtime.configuration
                              .capture_device_id)
                << " render_id="
                << (response.response.audio_runtime.configuration
                            .render_device_id.empty()
                        ? "default"
                        : response.response.audio_runtime.configuration
                              .render_device_id);
    }
  }
  if (response.response.has_virtual_asio_devices) {
    std::cout << " virtual_asio_devices="
              << response.response.virtual_asio_devices.size();
  }
  std::cout << '\n';
  if (response.response.has_devices) {
    for (std::size_t device_index = 0;
         device_index < response.response.devices.size();
         ++device_index) {
      const auto& device = response.response.devices[device_index];
      std::cout << "device index=" << device_index
                << " id=" << std::quoted(device.id)
                << " label=" << std::quoted(device.label)
                << " backend=" << backend_name(device.backend)
                << " direction=" << direction_name(device.direction)
                << " default=" << (device.is_default ? "true" : "false")
                << " virtual=" << (device.is_virtual ? "true" : "false")
                << " formats=" << device.formats.size() << '\n';
      for (std::size_t format_index = 0;
           format_index < device.formats.size();
           ++format_index) {
        const auto& format = device.formats[format_index];
        std::cout << "device_format device_index=" << device_index
                  << " index=" << format_index
                  << " sample_rate=" << format.sample_rate
                  << " channels=" << format.channels
                  << " frames=" << format.frames_per_block
                  << " bits=" << format.bits_per_sample
                  << " valid_bits=" << format.valid_bits_per_sample
                  << " format=" << sample_format_name(format.sample_format)
                  << '\n';
      }
    }
  }
  if (response.response.has_virtual_asio_devices) {
    for (std::size_t index = 0;
         index < response.response.virtual_asio_devices.size(); ++index) {
      const auto& device = response.response.virtual_asio_devices[index];
      std::cout << "virtual_asio_device index=" << index
                << " id=" << std::quoted(device.device_id)
                << " clsid=" << std::quoted(device.clsid)
                << " registry_name=" << std::quoted(device.registry_name)
                << " broker_token=" << std::quoted(device.broker_token)
                << " inputs=" << device.input_channels
                << " outputs=" << device.output_channels
                << " enabled=" << (device.enabled ? "true" : "false")
                << '\n';
    }
  }
  return 0;
}
