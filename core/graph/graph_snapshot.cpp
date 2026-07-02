#include "core/graph/graph_snapshot.h"

#include <utility>

namespace sar::graph {

GraphSnapshotPublisher::GraphSnapshotPublisher(std::shared_ptr<Graph> initial_graph) noexcept
    : graph_(std::move(initial_graph)) {}

void GraphSnapshotPublisher::publish(std::shared_ptr<Graph> graph) noexcept {
  graph_.store(std::move(graph), std::memory_order_release);
}

std::shared_ptr<Graph> GraphSnapshotPublisher::current() const noexcept {
  return graph_.load(std::memory_order_acquire);
}

void GraphSnapshotPublisher::process(const realtime::AudioBuffer& input,
                                     realtime::AudioBuffer& output,
                                     diagnostics::EngineDiagnostics& diagnostics) noexcept {
  const auto graph = current();
  if (graph == nullptr) {
    output.copy_from(input);
    return;
  }

  graph->process(input, output, diagnostics);
}

}  // namespace sar::graph

