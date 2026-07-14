#pragma once

#include "core/platform/wasapi_endpoint_selection_policy.h"
#include "core/platform/wasapi_recovery_policy.h"
#include "core/platform/windows_wasapi_realtime_worker.h"

#include <cstdint>
#include <functional>
#include <memory>
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
  [[nodiscard]] virtual std::vector<WasapiRealtimeWorkerError> last_errors() const = 0;
};

class WasapiDuplexRuntimeOpenResult {
 public:
  static WasapiDuplexRuntimeOpenResult success(std::unique_ptr<WasapiDuplexRuntime> runtime);
  static WasapiDuplexRuntimeOpenResult failure(std::vector<WasapiRealtimeWorkerError> errors);
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::unique_ptr<WasapiDuplexRuntime> take_runtime() noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  WasapiDuplexRuntimeOpenResult(std::unique_ptr<WasapiDuplexRuntime> runtime,
                               std::vector<WasapiRealtimeWorkerError> errors);
  std::unique_ptr<WasapiDuplexRuntime> runtime_;
  std::vector<WasapiRealtimeWorkerError> errors_;
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
  std::uint64_t capture_endpoint_generation = 0;
  std::uint64_t render_endpoint_generation = 0;
  std::uint64_t endpoint_notification_reopen_count = 0;
  std::uint64_t endpoint_notification_reset_failure_count = 0;
  bool endpoint_generations_initialized = false;
  bool running = false;
};

class WindowsWasapiDuplexSupervisor {
 public:
  WindowsWasapiDuplexSupervisor(WasapiDuplexRuntimeFactory factory,
                                std::uint32_t timeout_ms);
  WindowsWasapiDuplexSupervisor(
      WasapiDuplexRuntimeFactory factory,
      std::uint32_t timeout_ms,
      WasapiEndpointSelectionPolicy endpoint_selection_policy);
  WindowsWasapiDuplexSupervisor(graph::Graph& graph,
                                diagnostics::EngineDiagnostics& diagnostics,
                                std::uint32_t timeout_ms);
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
  [[nodiscard]] WasapiDuplexSupervisorSummary summary() const noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& last_errors() const noexcept;

 private:
  void attempt_open(std::uint64_t now_ms);
  void handle_failure(std::vector<WasapiRealtimeWorkerError> errors,
                      std::uint64_t now_ms);
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
  std::uint64_t endpoint_notification_reopen_count_ = 0;
  std::uint64_t endpoint_notification_reset_failure_count_ = 0;
  bool endpoint_generations_initialized_ = false;
  bool recovery_episode_active_ = false;
};

}  // namespace sar::platform
