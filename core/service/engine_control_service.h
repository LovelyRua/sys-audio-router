#pragma once

#include "core/control/control_session.h"
#include "core/control/control_wire_protocol.h"
#include "core/control/session_document.h"
#include "core/platform/audio_device_registry.h"
#include "core/service/engine_audio_runtime.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sar::service {

class EngineControlServiceCreateResult;

using EngineAudioRuntimeConfigurator = std::function<EngineAudioRuntimeBuildResult(
    const control::AudioRuntimeConfiguration&,
    std::shared_ptr<graph::Graph>)>;
using EnginePresetCommitObserver = std::function<std::vector<control::PresetError>(
    const control::PresetDocument&, std::uint64_t)>;

class EngineControlService {
 public:
  static EngineControlServiceCreateResult create(
      control::PresetDocument initial_preset,
      std::uint64_t initial_graph_version = 1);
  ~EngineControlService();

  [[nodiscard]] EngineAudioRuntimeResult install_audio_runtime(
      std::unique_ptr<EngineAudioRuntime> runtime,
      EngineAudioRuntimeBuilder builder = {});
  [[nodiscard]] EngineAudioRuntimeResult start_audio_runtime(
      std::uint32_t timeout_ms = 10);
  void set_audio_runtime_configurator(
      EngineAudioRuntimeConfigurator configurator);
  void set_preset_commit_observer(EnginePresetCommitObserver observer);
  [[nodiscard]] EngineAudioRuntimeResult configure_audio_runtime(
      control::AudioRuntimeConfiguration configuration);
  void add_audio_device_provider(
      std::unique_ptr<platform::AudioDeviceProvider> provider);
  void stop_audio_runtime() noexcept;
  [[nodiscard]] bool has_audio_runtime() const noexcept;
  [[nodiscard]] bool audio_runtime_running() const noexcept;
  [[nodiscard]] diagnostics::EngineDiagnostics audio_runtime_diagnostics()
      const;
  [[nodiscard]] control::SessionDocument session_document() const;
  [[nodiscard]] std::unique_ptr<graph::Graph> build_client_graph(
      std::uint32_t sample_rate,
      std::uint32_t frames_per_block,
      std::uint32_t input_channels,
      std::uint32_t output_channels) const;

  [[nodiscard]] control::ControlWireEncodeResult handle_wire_request(
      std::span<const std::uint8_t> request);
  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

  [[nodiscard]] const control::ControlSession& session() const noexcept;

 private:
  explicit EngineControlService(
      std::unique_ptr<control::ControlSession> session) noexcept;
  [[nodiscard]] EngineAudioRuntimeResult start_audio_runtime_locked(
      std::uint32_t timeout_ms);
  [[nodiscard]] EngineAudioRuntimeBuildResult build_audio_runtime_locked(
      std::shared_ptr<graph::Graph> graph);
  [[nodiscard]] EngineAudioRuntimeResult rebuild_audio_runtime_locked();
  [[nodiscard]] EngineAudioRuntimeResult configure_audio_runtime_locked(
      control::AudioRuntimeConfiguration configuration);
  void stop_audio_runtime_locked() noexcept;
  [[nodiscard]] control::ControlResponse audio_runtime_state_response_locked(
      std::string command_id) const;
  [[nodiscard]] control::ControlResponse append_platform_devices_locked(
      control::ControlResponse response) const;
  [[nodiscard]] const control::ControlWireEncodeResult*
  find_replayed_response_locked(std::string_view command_id) const noexcept;
  void remember_response_locked(
      std::string command_id,
      const control::ControlWireEncodeResult& response);

  struct ReplayedResponse {
    std::string command_id;
    control::ControlWireEncodeResult response;
  };

  static constexpr std::size_t kMaxReplayedResponses = 64;
  static constexpr std::size_t kMaxReplayedResponseBytes = 4 * 1024 * 1024;

  std::unique_ptr<control::ControlSession> session_;
  std::unique_ptr<EngineAudioRuntime> audio_runtime_;
  EngineAudioRuntimeBuilder audio_runtime_builder_;
  EngineAudioRuntimeConfigurator audio_runtime_configurator_;
  EnginePresetCommitObserver preset_commit_observer_;
  bool preset_commit_in_progress_ = false;
  std::optional<control::AudioRuntimeConfiguration>
      audio_runtime_configuration_;
  platform::AudioDeviceRegistry audio_device_registry_;
  std::deque<ReplayedResponse> replayed_responses_;
  std::size_t replayed_response_bytes_ = 0;
  // The preset observer is a service extension point. It may query service
  // state while a commit is in progress, so this lock must permit same-thread
  // re-entry without blocking the single control-pipe request.
  mutable std::recursive_mutex control_mutex_;
};

class EngineControlServiceCreateResult {
 public:
  static EngineControlServiceCreateResult success(
      std::unique_ptr<EngineControlService> service);
  static EngineControlServiceCreateResult failure(
      std::vector<control::PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] EngineControlService& service() noexcept;
  [[nodiscard]] std::unique_ptr<EngineControlService> take_service() noexcept;
  [[nodiscard]] const std::vector<control::PresetError>& errors() const noexcept;

 private:
  EngineControlServiceCreateResult(
      std::unique_ptr<EngineControlService> service,
      std::vector<control::PresetError> errors) noexcept;

  std::unique_ptr<EngineControlService> service_;
  std::vector<control::PresetError> errors_;
};

}  // namespace sar::service
