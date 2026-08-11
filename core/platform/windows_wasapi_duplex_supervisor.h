#pragma once

#include "core/platform/realtime_audio_source.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "core/platform/wasapi_endpoint_selection_policy.h"
#include "core/platform/wasapi_recovery_policy.h"
#include "core/platform/windows_wasapi_realtime_worker.h"
#include "core/platform/windows_wasapi_runtime_summary.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sar::diagnostics { struct EngineDiagnostics; }
namespace sar::graph { class Graph; }

namespace sar::platform {

class WindowsWasapiEndpointNotification;

class WasapiDuplexRuntime {
 public:
  virtual ~WasapiDuplexRuntime() = default;
  [[nodiscard]] virtual WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual WasapiRealtimeWorkerStats stats() const noexcept = 0;
  [[nodiscard]] virtual std::vector<WasapiRealtimeWorkerError> last_errors() const = 0;
  [[nodiscard]] virtual WasapiRuntimeSummary runtime_summary() const;
};

struct WasapiDuplexRuntimeEndpoints {
  std::string capture_device_id;
  std::string render_device_id;
};

class WasapiDuplexRuntimeOpenResult {
 public:
  static WasapiDuplexRuntimeOpenResult success(
      std::unique_ptr<WasapiDuplexRuntime> runtime,
      WasapiDuplexRuntimeEndpoints endpoints = {});
  static WasapiDuplexRuntimeOpenResult failure(std::vector<WasapiRealtimeWorkerError> errors);
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::unique_ptr<WasapiDuplexRuntime> take_runtime() noexcept;
  [[nodiscard]] const WasapiDuplexRuntimeEndpoints& endpoints() const noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  WasapiDuplexRuntimeOpenResult(std::unique_ptr<WasapiDuplexRuntime> runtime,
                               std::vector<WasapiRealtimeWorkerError> errors,
                               WasapiDuplexRuntimeEndpoints endpoints);
  std::unique_ptr<WasapiDuplexRuntime> runtime_;
  std::vector<WasapiRealtimeWorkerError> errors_;
  WasapiDuplexRuntimeEndpoints endpoints_;
};

using WasapiDuplexRuntimeFactory = std::function<WasapiDuplexRuntimeOpenResult()>;

[[nodiscard]] WasapiFailureClass classify_wasapi_failures(
    const std::vector<WasapiRealtimeWorkerError>& errors) noexcept;

struct WasapiDuplexSupervisorSummary {
  WasapiRecoveryState state = WasapiRecoveryState::Stopped;
  std::uint32_t attempt_count = 0;
  std::uint64_t next_attempt_at_ms = 0;
  std::uint64_t recovery_deadline_at_ms = 0;
  std::size_t error_count = 0;
  std::uint64_t runtime_open_count = 0;
  std::uint64_t recovery_episode_count = 0;
  std::uint64_t successful_recovery_count = 0;
  std::uint64_t failed_recovery_count = 0;
  std::uint64_t last_recovery_duration_ms = 0;
  std::uint64_t maximum_recovery_duration_ms = 0;
  std::uint64_t maximum_render_recovery_silence_frames = 0;
  std::uint64_t capture_endpoint_generation = 0;
  std::uint64_t render_endpoint_generation = 0;
  std::uint64_t endpoint_notification_reopen_count = 0;
  std::uint64_t endpoint_notification_reset_failure_count = 0;
  std::uint64_t endpoint_notification_reopen_at_ms = 0;
  bool endpoint_generations_initialized = false;
  bool endpoint_notification_reopen_pending = false;
  std::string active_capture_device_id;
  std::string active_render_device_id;
  bool running = false;
};

class WindowsWasapiDuplexSupervisor {
 public:
  static constexpr std::uint64_t kEndpointNotificationSettleMs = 300;

  WindowsWasapiDuplexSupervisor(WasapiDuplexRuntimeFactory factory,
                                std::uint32_t timeout_ms);
  WindowsWasapiDuplexSupervisor(
      WasapiDuplexRuntimeFactory factory,
      std::uint32_t timeout_ms,
      WasapiEndpointSelectionPolicy endpoint_selection_policy);
  WindowsWasapiDuplexSupervisor(graph::Graph& graph,
                                diagnostics::EngineDiagnostics& diagnostics,
                                std::uint32_t timeout_ms,
                                RealtimeAudioSource* external_input = nullptr,
                                RealtimeAudioSink* external_output = nullptr,
                                WasapiGraphChannelLayout channel_layout = {});
  WindowsWasapiDuplexSupervisor(
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      std::uint32_t timeout_ms,
      WasapiEndpointSelectionPolicy endpoint_selection_policy,
      RealtimeAudioSource* external_input = nullptr,
      RealtimeAudioSink* external_output = nullptr,
      WasapiGraphChannelLayout channel_layout = {});
  WindowsWasapiDuplexSupervisor(const WindowsWasapiDuplexSupervisor&) = delete;
  WindowsWasapiDuplexSupervisor& operator=(const WindowsWasapiDuplexSupervisor&) = delete;
  ~WindowsWasapiDuplexSupervisor();

  void start(std::uint64_t now_ms);
  void tick(std::uint64_t now_ms);
  [[nodiscard]] WasapiEndpointReopenRequirements poll_endpoint_notifications(
      WindowsWasapiEndpointNotification& notifications,
      std::uint64_t now_ms);
  void request_reopen(std::uint64_t now_ms);
  void stop(std::uint64_t now_ms) noexcept;
  [[nodiscard]] WasapiRecoveryState state() const noexcept;
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] WasapiDuplexSupervisorSummary summary() const;
  [[nodiscard]] WasapiRealtimeWorkerStats runtime_stats() const noexcept;
  [[nodiscard]] WasapiRuntimeSummary runtime_summary() const;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& last_errors() const noexcept;

 private:
  void attempt_open(std::uint64_t now_ms);
  void handle_failure(std::vector<WasapiRealtimeWorkerError> errors,
                      std::uint64_t now_ms);
  void retain_runtime_diagnostics() noexcept;
  void quiesce(std::uint64_t now_ms) noexcept;

  WasapiDuplexRuntimeFactory factory_;
  std::uint32_t timeout_ms_ = 0;
  WasapiEndpointSelectionPolicy endpoint_selection_policy_;
  WasapiDefaultEndpointGenerations endpoint_generations_;
  WasapiRecoveryPolicy policy_;
  std::unique_ptr<WasapiDuplexRuntime> runtime_;
  std::vector<WasapiRealtimeWorkerError> last_errors_;
  std::uint64_t runtime_open_count_ = 0;
  std::uint64_t recovery_episode_count_ = 0;
  std::uint64_t successful_recovery_count_ = 0;
  std::uint64_t failed_recovery_count_ = 0;
  std::uint64_t recovery_started_at_ms_ = 0;
  std::uint64_t last_recovery_duration_ms_ = 0;
  std::uint64_t maximum_recovery_duration_ms_ = 0;
  std::uint64_t maximum_render_recovery_silence_frames_ = 0;
  std::uint64_t endpoint_notification_reopen_count_ = 0;
  std::uint64_t endpoint_notification_reset_failure_count_ = 0;
  std::uint64_t endpoint_notification_reopen_at_ms_ = 0;
  bool endpoint_generations_initialized_ = false;
  bool endpoint_notification_reopen_pending_ = false;
  WasapiDuplexRuntimeEndpoints active_endpoints_;
  bool recovery_episode_active_ = false;
};

}  // namespace sar::platform
