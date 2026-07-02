#pragma once

#include "core/graph/graph.h"
#include "core/graph/node.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::graph {

struct GraphBuildError {
  std::string code;
  std::string message;
};

class GraphBuildResult {
 public:
  static GraphBuildResult success(std::unique_ptr<Graph> graph);
  static GraphBuildResult failure(std::vector<GraphBuildError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] Graph* graph() const noexcept;
  [[nodiscard]] std::unique_ptr<Graph> take_graph() noexcept;
  [[nodiscard]] const std::vector<GraphBuildError>& errors() const noexcept;

 private:
  GraphBuildResult(std::unique_ptr<Graph> graph, std::vector<GraphBuildError> errors);

  std::unique_ptr<Graph> graph_;
  std::vector<GraphBuildError> errors_;
};

class GraphBuilder {
 public:
  GraphBuilder(std::uint64_t version, std::size_t channels, std::size_t frames);

  GraphBuilder& sample_rate(std::uint32_t sample_rate) noexcept;
  GraphBuilder& add_node(std::unique_ptr<Node> node);
  GraphBuilder& add_node(std::string label, std::unique_ptr<Node> node);

  [[nodiscard]] GraphBuildResult build();

 private:
  struct PendingNode {
    std::string label;
    std::unique_ptr<Node> node;
  };

  void validate(std::vector<GraphBuildError>& errors) const;

  std::uint64_t version_;
  std::size_t channels_;
  std::size_t frames_;
  std::uint32_t sample_rate_ = 48000;
  std::vector<PendingNode> nodes_;
};

}  // namespace sar::graph
