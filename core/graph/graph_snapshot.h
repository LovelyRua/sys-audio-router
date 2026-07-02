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
  std::atomic<std::shared_ptr<Graph>> graph_;
};

}  // namespace sar::graph

