#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/realtime/audio_buffer.h"

#include <atomic>
#include <memory>

namespace sar::graph {

class GraphSnapshotPublisher {
 public:
  explicit GraphSnapshotPublisher(std::shared_ptr<Graph> initial_graph) noexcept;

  void publish(std::shared_ptr<Graph> graph) noexcept;
  [[nodiscard]] std::shared_ptr<Graph> current() const noexcept;

  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

 private:
  // Ownership and reclamation stay on the control/publisher side. The audio
  // path only observes this raw pointer while holding an active reader slot.
  std::shared_ptr<Graph> graph_;
  std::atomic<Graph*> active_graph_ = nullptr;
  std::atomic_uint32_t active_processors_ = 0;
};

}  // namespace sar::graph
