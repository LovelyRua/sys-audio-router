#include "core/graph/graph_builder.h"

#include <unordered_set>
#include <utility>

namespace sar::graph {

GraphBuildResult GraphBuildResult::success(std::unique_ptr<Graph> graph) {
  return {std::move(graph), {}};
}

GraphBuildResult GraphBuildResult::failure(std::vector<GraphBuildError> errors) {
  return {nullptr, std::move(errors)};
}

bool GraphBuildResult::ok() const noexcept {
  return graph_ != nullptr && errors_.empty();
}

Graph* GraphBuildResult::graph() const noexcept {
  return graph_.get();
}

std::unique_ptr<Graph> GraphBuildResult::take_graph() noexcept {
  return std::move(graph_);
}

const std::vector<GraphBuildError>& GraphBuildResult::errors() const noexcept {
  return errors_;
}

GraphBuildResult::GraphBuildResult(std::unique_ptr<Graph> graph,
                                   std::vector<GraphBuildError> errors)
    : graph_(std::move(graph)), errors_(std::move(errors)) {}

GraphBuilder::GraphBuilder(std::uint64_t version, std::size_t channels, std::size_t frames)
    : version_(version), channels_(channels), frames_(frames) {}

GraphBuilder& GraphBuilder::sample_rate(std::uint32_t sample_rate) noexcept {
  sample_rate_ = sample_rate;
  return *this;
}

GraphBuilder& GraphBuilder::add_node(std::unique_ptr<Node> node) {
  const auto default_name = "node_" + std::to_string(nodes_.size());
  return add_node(default_name, default_name, std::move(node));
}

GraphBuilder& GraphBuilder::add_node(std::string label, std::unique_ptr<Node> node) {
  return add_node(label, label, std::move(node));
}

GraphBuilder& GraphBuilder::add_node(std::string id,
                                     std::string label,
                                     std::unique_ptr<Node> node) {
  nodes_.push_back({std::move(id), std::move(label), std::move(node)});
  return *this;
}

GraphBuildResult GraphBuilder::build() {
  std::vector<GraphBuildError> errors;
  validate(errors);

  if (!errors.empty()) {
    return GraphBuildResult::failure(std::move(errors));
  }

  auto graph = std::make_unique<Graph>(version_, channels_, frames_, sample_rate_);
  for (auto& pending : nodes_) {
    graph->add_node(std::move(pending.id), std::move(pending.label), std::move(pending.node));
  }

  nodes_.clear();
  return GraphBuildResult::success(std::move(graph));
}

void GraphBuilder::validate(std::vector<GraphBuildError>& errors) const {
  if (version_ == 0) {
    errors.push_back({"invalid_version", "Graph version must be non-zero."});
  }

  if (channels_ == 0) {
    errors.push_back({"invalid_channels", "Graph channel count must be non-zero."});
  }

  if (frames_ == 0) {
    errors.push_back({"invalid_frames", "Graph frame count must be non-zero."});
  }

  if (sample_rate_ == 0) {
    errors.push_back({"invalid_sample_rate", "Graph sample rate must be non-zero."});
  }

  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> labels;
  for (const auto& pending : nodes_) {
    if (pending.node == nullptr) {
      errors.push_back({"null_node", "Graph node list contains a null node."});
    }
    if (pending.id.empty()) {
      errors.push_back({"empty_node_id", "Graph node IDs must not be empty."});
    } else if (!ids.insert(pending.id).second) {
      errors.push_back({"duplicate_node_id", "Graph node IDs must be unique."});
    }
    if (pending.label.empty()) {
      errors.push_back({"empty_node_label", "Graph node labels must not be empty."});
    } else if (!labels.insert(pending.label).second) {
      errors.push_back({"duplicate_node_label", "Graph node labels must be unique."});
    }
  }
}

}  // namespace sar::graph
