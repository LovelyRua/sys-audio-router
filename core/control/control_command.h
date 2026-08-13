#pragma once

#include "core/control/preset_document.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sar::control {

enum class ControlCommandType {
  ListDevices,
  CreateVirtualEndpoint,
  RemoveVirtualEndpoint,
  ConnectRoute,
  DisconnectRoute,
  SetGain,
  SetMute,
  LoadPreset,
  SavePreset,
  QueryDiagnostics,
  QueryActiveGraph,
  QuerySessionState,
  QueryAudioRuntime,
  StartAudioRuntime,
  StopAudioRuntime,
  ConfigureAudioRuntime,
};

enum class AudioRuntimeMode {
  None,
  WasapiRender,
  WasapiDuplex,
  WasapiMatrix,
};

enum class AudioRuntimeEndpointDirection {
  Capture,
  Render,
};

struct AudioRuntimeEndpointConfiguration {
  // This stable ID belongs to graph routes. device_id is a native binding and
  // may change when hardware is unplugged and reconnected.
  std::string endpoint_id;
  std::string device_id;
  AudioRuntimeEndpointDirection direction =
      AudioRuntimeEndpointDirection::Capture;
  bool clock_master = false;
  std::uint32_t first_channel = 0;
  std::uint32_t channel_count = 0;
};

struct AudioRuntimeConfiguration {
  AudioRuntimeMode mode = AudioRuntimeMode::None;
  std::string capture_device_id;
  std::string render_device_id;
  std::vector<AudioRuntimeEndpointConfiguration> endpoints;
};

inline constexpr std::size_t kMaximumAudioRuntimeEndpoints = 32;

struct ControlCommand {
  std::uint32_t schema_version = 1;
  std::string command_id;
  ControlCommandType type = ControlCommandType::QueryDiagnostics;

  std::string endpoint_id;
  std::string endpoint_label;
  std::string input_id;
  std::string output_id;
  float gain = 0.0F;
  bool mute = false;
  PresetDocument preset;
  AudioRuntimeConfiguration audio_runtime;
};

class ControlCommandValidationResult {
 public:
  static ControlCommandValidationResult success();
  static ControlCommandValidationResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  explicit ControlCommandValidationResult(std::vector<PresetError> errors);

  std::vector<PresetError> errors_;
};

class ControlApplyResult {
 public:
  static ControlApplyResult success(PresetDocument document);
  static ControlApplyResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const PresetDocument& document() const noexcept;
  [[nodiscard]] PresetDocument take_document() noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  ControlApplyResult(PresetDocument document, std::vector<PresetError> errors);

  PresetDocument document_;
  std::vector<PresetError> errors_;
};

[[nodiscard]] ControlCommandValidationResult validate_command(const ControlCommand& command);
[[nodiscard]] std::vector<PresetError> validate_audio_runtime_configuration(
    const AudioRuntimeConfiguration& configuration,
    bool allow_none);
[[nodiscard]] bool control_command_mutates_preset(
    ControlCommandType type) noexcept;
[[nodiscard]] ControlApplyResult apply_command(const PresetDocument& current,
                                               const ControlCommand& command);

}  // namespace sar::control
