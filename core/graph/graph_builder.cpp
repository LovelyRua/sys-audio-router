#include "core/graph/graph_builder.h"

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
  nodes_.push_back(std::move(node));
  return *this;
}

GraphBuildResult GraphBuilder::build() {
  std::vector<GraphBuildError> errors;
  validate(errors);

  if (!errors.empty()) {
    return GraphBuildResult::failure(std::move(errors));
  }

  auto graph = std::make_unique<Graph>(version_, channels_, frames_, sample_rate_);
  for (auto& node : nodes_) {
    graph->add_node(std::move(node));
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

  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    if (nodes_[index] == nullptr) {
      errors.push_back({"null_node", "Graph node list contains a null node."});
    }
  }
}

}  // namespace sar::graph

