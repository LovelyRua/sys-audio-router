#pragma once

#include "core/graph/graph.h"
#include "core/graph/route_matrix.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::control {

struct PresetError {
  std::string code;
  std::string message;
};

struct PresetNode {
  std::string id;
  std::string label;
  std::string type;
};

struct PresetRoute {
  std::string input_id;
  std::string output_id;
  float gain = 0.0F;
  bool muted = false;
};

struct PresetRouteMatrix {
  std::vector<graph::RouteEndpointDescriptor> inputs;
  std::vector<graph::RouteEndpointDescriptor> outputs;
  std::vector<PresetRoute> routes;
};

struct PresetDocument {
  std::uint32_t schema_version = 1;
  std::uint32_t sample_rate = 48000;
  std::uint32_t frames_per_block = 128;
  std::vector<PresetNode> nodes;
  PresetRouteMatrix matrix;
};

class PresetValidationResult {
 public:
  static PresetValidationResult success();
  static PresetValidationResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  explicit PresetValidationResult(std::vector<PresetError> errors);

  std::vector<PresetError> errors_;
};

class PresetMatrixBuildResult {
 public:
  static PresetMatrixBuildResult success(std::unique_ptr<graph::RouteMatrix> matrix);
  static PresetMatrixBuildResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] graph::RouteMatrix* matrix() const noexcept;
  [[nodiscard]] std::unique_ptr<graph::RouteMatrix> take_matrix() noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  PresetMatrixBuildResult(std::unique_ptr<graph::RouteMatrix> matrix,
                          std::vector<PresetError> errors);

  std::unique_ptr<graph::RouteMatrix> matrix_;
  std::vector<PresetError> errors_;
};

class PresetGraphBuildResult {
 public:
  static PresetGraphBuildResult success(std::unique_ptr<graph::Graph> graph);
  static PresetGraphBuildResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] graph::Graph* graph() const noexcept;
  [[nodiscard]] std::unique_ptr<graph::Graph> take_graph() noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  PresetGraphBuildResult(std::unique_ptr<graph::Graph> graph,
                         std::vector<PresetError> errors);

  std::unique_ptr<graph::Graph> graph_;
  std::vector<PresetError> errors_;
};

[[nodiscard]] PresetValidationResult validate_preset(const PresetDocument& preset);
[[nodiscard]] PresetMatrixBuildResult build_route_matrix(const PresetDocument& preset);
[[nodiscard]] PresetGraphBuildResult build_preset_graph(const PresetDocument& preset,
                                                        std::uint64_t graph_version = 1);

}  // namespace sar::control
