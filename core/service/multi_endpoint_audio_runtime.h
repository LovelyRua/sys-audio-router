#pragma once

#include "core/service/engine_audio_runtime.h"

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sar::service {

struct AudioRuntimeMember {
  std::string endpoint_id;
  std::unique_ptr<EngineAudioRuntime> runtime;
};

// Owns one graph-driving master and zero or more independently clocked device
// followers. Followers consume or produce preallocated endpoint queues; only
// the master is allowed to advance the product graph.
class MultiEndpointAudioRuntime final : public EngineAudioRuntime {
 public:
  MultiEndpointAudioRuntime(AudioRuntimeMember master,
                            std::vector<AudioRuntimeMember> followers);
  MultiEndpointAudioRuntime(const MultiEndpointAudioRuntime&) = delete;
  MultiEndpointAudioRuntime& operator=(const MultiEndpointAudioRuntime&) = delete;
  ~MultiEndpointAudioRuntime() override;

  [[nodiscard]] EngineAudioRuntimeResult start(
      std::uint32_t timeout_ms) override;
  void stop() noexcept override;
  [[nodiscard]] bool running() const noexcept override;
  [[nodiscard]] std::uint64_t graph_version() const noexcept override;
  [[nodiscard]] bool apply_realtime_graph_parameters(
      const graph::Graph& graph) noexcept override;
  [[nodiscard]] diagnostics::EngineDiagnostics diagnostics() const override;
  [[nodiscard]] std::optional<EngineAudioRecoveryDiagnostics>
  recovery_diagnostics() const override;

  [[nodiscard]] std::size_t endpoint_count() const noexcept;
  [[nodiscard]] std::string_view master_endpoint_id() const noexcept;

 private:
  void stop_locked() noexcept;

  AudioRuntimeMember master_;
  std::vector<AudioRuntimeMember> followers_;
  mutable std::mutex lifecycle_mutex_;
  bool running_ = false;
};

}  // namespace sar::service
