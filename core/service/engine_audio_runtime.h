#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sar::service {

struct EngineAudioRuntimeError {
  std::string code;
  std::string message;
  std::optional<std::int32_t> native_hresult = std::nullopt;
  std::optional<std::uint32_t> native_win32_code = std::nullopt;
};

enum class EngineAudioRecoveryState {
  Stopped,
  Opening,
  Running,
  Quiescing,
  Backoff,
  Faulted,
};

enum class EngineAudioRuntimeHealth {
  Stopped,
  Healthy,
  Degraded,
  Faulted,
};

struct EngineAudioRecoveryDiagnostics {
  EngineAudioRecoveryState state = EngineAudioRecoveryState::Stopped;
  EngineAudioRuntimeHealth runtime_health = EngineAudioRuntimeHealth::Stopped;
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

class EngineAudioRuntimeResult {
 public:
  static EngineAudioRuntimeResult success();
  static EngineAudioRuntimeResult failure(
      std::vector<EngineAudioRuntimeError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<EngineAudioRuntimeError>& errors() const noexcept;

 private:
  explicit EngineAudioRuntimeResult(
      std::vector<EngineAudioRuntimeError> errors) noexcept;

  std::vector<EngineAudioRuntimeError> errors_;
};

class EngineAudioRuntime {
 public:
  EngineAudioRuntime(const EngineAudioRuntime&) = delete;
  EngineAudioRuntime& operator=(const EngineAudioRuntime&) = delete;
  virtual ~EngineAudioRuntime() = default;

  [[nodiscard]] virtual EngineAudioRuntimeResult start(
      std::uint32_t timeout_ms) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t graph_version() const noexcept = 0;
  [[nodiscard]] virtual bool apply_realtime_graph_parameters(
      const graph::Graph& graph) noexcept {
    (void)graph;
    return false;
  }
  [[nodiscard]] virtual diagnostics::EngineDiagnostics diagnostics() const = 0;
  [[nodiscard]] virtual std::optional<EngineAudioRecoveryDiagnostics>
  recovery_diagnostics() const {
    return std::nullopt;
  }

 protected:
  EngineAudioRuntime() = default;
};

class EngineAudioRuntimeBuildResult {
 public:
  static EngineAudioRuntimeBuildResult success(
      std::unique_ptr<EngineAudioRuntime> runtime);
  static EngineAudioRuntimeBuildResult failure(
      std::vector<EngineAudioRuntimeError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::unique_ptr<EngineAudioRuntime> take_runtime() noexcept;
  [[nodiscard]] const std::vector<EngineAudioRuntimeError>& errors() const noexcept;

 private:
  EngineAudioRuntimeBuildResult(
      std::unique_ptr<EngineAudioRuntime> runtime,
      std::vector<EngineAudioRuntimeError> errors) noexcept;

  std::unique_ptr<EngineAudioRuntime> runtime_;
  std::vector<EngineAudioRuntimeError> errors_;
};

using EngineAudioRuntimeBuilder = std::function<EngineAudioRuntimeBuildResult(
    std::shared_ptr<graph::Graph>)>;

}  // namespace sar::service
