#include "core/graph/graph_snapshot.h"

#include <thread>
#include <utility>

namespace sar::graph {

GraphSnapshotPublisher::GraphSnapshotPublisher(std::shared_ptr<Graph> initial_graph) noexcept
    : graph_(std::move(initial_graph)) {
  active_graph_.store(graph_.get(), std::memory_order_release);
}

void GraphSnapshotPublisher::publish(std::shared_ptr<Graph> graph) noexcept {
  auto previous = std::move(graph_);
  graph_ = std::move(graph);
  active_graph_.store(graph_.get(), std::memory_order_seq_cst);
  while (active_processors_.load(std::memory_order_seq_cst) != 0) {
    std::this_thread::yield();
  }
  previous.reset();
}

std::shared_ptr<Graph> GraphSnapshotPublisher::current() const noexcept {
  return graph_;
}

void GraphSnapshotPublisher::process(const realtime::AudioBuffer& input,
                                     realtime::AudioBuffer& output,
                                     diagnostics::EngineDiagnostics& diagnostics) noexcept {
  active_processors_.fetch_add(1, std::memory_order_seq_cst);
  auto* graph = active_graph_.load(std::memory_order_seq_cst);
  if (graph == nullptr) {
    output.copy_from(input);
    active_processors_.fetch_sub(1, std::memory_order_seq_cst);
    return;
  }

  graph->process(input, output, diagnostics);
  active_processors_.fetch_sub(1, std::memory_order_seq_cst);
}

}  // namespace sar::graph
