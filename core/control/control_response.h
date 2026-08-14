#pragma once

#include "core/control/control_command.h"
#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/audio_device.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sar::control {

enum class ControlResponseStatus {
  Accepted,
  Rejected,
};

enum class WasapiRecoveryState {
  Stopped,
  Opening,
  Running,
  Quiescing,
  Backoff,
  Faulted,
};

enum class WasapiRuntimeHealth {
  Stopped,
  Healthy,
  Degraded,
  Faulted,
};

struct WasapiRecoveryDiagnostics {
  WasapiRecoveryState state = WasapiRecoveryState::Stopped;
  WasapiRuntimeHealth runtime_health = WasapiRuntimeHealth::Stopped;
  std::string runtime_reason_code;
  std::uint64_t recovery_episode_count = 0;
  std::uint64_t successful_recovery_count = 0;
  std::uint64_t failed_recovery_count = 0;
  std::uint64_t last_recovery_duration_ms = 0;
  std::uint64_t maximum_recovery_duration_ms = 0;
  std::uint64_t endpoint_notification_reopen_count = 0;
  std::uint64_t endpoint_notification_reset_failure_count = 0;
  bool endpoint_notification_reopen_pending = false;
  std::uint64_t wait_timeout_cycles = 0;
  std::uint64_t capture_discontinuity_cycles = 0;
  std::uint64_t render_fifo_underflow_frames = 0;
  std::uint64_t maximum_render_recovery_silence_frames = 0;
  std::uint64_t maximum_consecutive_capture_rate_clamped_frames = 0;
};

enum class AudioEndpointRuntimeRole {
  Master,
  Follower,
};

struct AudioEndpointRuntimeDiagnostics {
  std::string endpoint_id;
  AudioEndpointRuntimeRole role = AudioEndpointRuntimeRole::Follower;
  diagnostics::EngineDiagnostics diagnostics;
  std::optional<WasapiRecoveryDiagnostics> recovery;
  std::optional<std::uint64_t> queue_fill_frames;
  std::optional<double> correction_ppm;
};

struct ControlResponse {
  std::string command_id;
  ControlResponseStatus status = ControlResponseStatus::Accepted;
  std::vector<PresetError> errors;
  PresetDocument preset;
  bool has_preset = false;
  diagnostics::EngineDiagnostics diagnostics;
  bool has_diagnostics = false;
  WasapiRecoveryDiagnostics wasapi_recovery;
  bool has_wasapi_recovery = false;
  std::vector<AudioEndpointRuntimeDiagnostics> endpoint_diagnostics;
  std::vector<platform::AudioDeviceDescriptor> devices;
  bool has_devices = false;
  std::uint64_t next_graph_version = 0;
  bool has_session_state = false;
  struct ActiveGraphNode {
    std::string id;
    std::string label;
  };
  struct ActiveGraphSummary {
    std::uint64_t version = 0;
    std::uint32_t sample_rate = 0;
    std::size_t channels = 0;
    std::size_t frames = 0;
    std::vector<ActiveGraphNode> nodes;
  };
  ActiveGraphSummary active_graph;
  bool has_active_graph = false;
  struct AudioRuntimeState {
    bool installed = false;
    bool running = false;
    std::uint64_t graph_version = 0;
    bool configured = false;
    AudioRuntimeConfiguration configuration;
  };
  AudioRuntimeState audio_runtime;
  bool has_audio_runtime_state = false;
  std::vector<VirtualAsioDeviceDefinition> virtual_asio_devices;
  bool has_virtual_asio_devices = false;
};

[[nodiscard]] ControlResponse command_accepted(std::string command_id);
[[nodiscard]] ControlResponse command_rejected(std::string command_id,
                                               std::vector<PresetError> errors);
[[nodiscard]] ControlResponse preset_response(std::string command_id,
                                              PresetDocument preset);
[[nodiscard]] ControlResponse diagnostics_response(
    std::string command_id,
    diagnostics::EngineDiagnostics diagnostics);
[[nodiscard]] ControlResponse device_list_response(
    std::string command_id,
    std::vector<platform::AudioDeviceDescriptor> devices);
[[nodiscard]] ControlResponse active_graph_response(std::string command_id,
                                                    const graph::Graph& graph);
[[nodiscard]] ControlResponse session_state_response(
    std::string command_id,
    PresetDocument preset,
    std::vector<platform::AudioDeviceDescriptor> devices,
    const graph::Graph& graph,
    std::uint64_t next_graph_version);
[[nodiscard]] ControlResponse audio_runtime_state_response(
    std::string command_id,
    bool installed,
    bool running,
    std::uint64_t graph_version);

}  // namespace sar::control
