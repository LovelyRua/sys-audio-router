#include "core/control/preset_document.h"

#include "core/graph/graph_builder.h"

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace sar::control {

namespace {

constexpr std::uint32_t kMaximumSampleRate = 384000;
constexpr std::uint32_t kMaximumFramesPerBlock = 8192;
constexpr std::size_t kMaximumNodes = 64;
constexpr std::size_t kMaximumMatrixChannels = 64;
constexpr std::size_t kMaximumRoutes =
    kMaximumMatrixChannels * kMaximumMatrixChannels;

void validate_identifier(const std::string& value,
                         const char* empty_code,
                         const char* empty_message,
                         std::vector<PresetError>& errors) {
  if (value.empty()) {
    errors.push_back({empty_code, empty_message});
  }
}

void validate_unique_endpoint_ids(const std::vector<graph::RouteEndpointDescriptor>& endpoints,
                                  const char* duplicate_code,
                                  const char* duplicate_message,
                                  std::vector<PresetError>& errors,
                                  std::unordered_set<std::string>& ids) {
  for (const auto& endpoint : endpoints) {
    if (endpoint.id.empty()) {
      errors.push_back({"empty_endpoint_id", "Route endpoint IDs must not be empty."});
    } else if (!ids.insert(endpoint.id).second) {
      errors.push_back({duplicate_code, duplicate_message});
    }

    if (endpoint.label.empty()) {
      errors.push_back({"empty_endpoint_label", "Route endpoint labels must not be empty."});
    }
  }
}

const PresetNode* find_route_matrix_node(const PresetDocument& preset,
                                         std::vector<PresetError>& errors) {
  const PresetNode* matrix_node = nullptr;

  for (const auto& node : preset.nodes) {
    if (node.type == "route_matrix") {
      if (matrix_node != nullptr) {
        errors.push_back({
            "multiple_route_matrix_nodes",
            "Preset graph build currently supports exactly one route_matrix node.",
        });
      } else {
        matrix_node = &node;
      }
      continue;
    }

    errors.push_back({
        "unsupported_preset_node_type",
        "Preset graph build currently supports route_matrix nodes only.",
    });
  }

  if (matrix_node == nullptr) {
    errors.push_back({
        "missing_route_matrix_node",
        "Preset graph build requires one route_matrix node.",
    });
  }

  return matrix_node;
}

void append_graph_build_errors(const graph::GraphBuildResult& result,
                               std::vector<PresetError>& errors) {
  for (const auto& error : result.errors()) {
    errors.push_back({
        "graph_" + error.code,
        error.message,
    });
  }
}

}  // namespace

PresetValidationResult PresetValidationResult::success() {
  return PresetValidationResult({});
}

PresetValidationResult PresetValidationResult::failure(std::vector<PresetError> errors) {
  return PresetValidationResult(std::move(errors));
}

bool PresetValidationResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<PresetError>& PresetValidationResult::errors() const noexcept {
  return errors_;
}

PresetValidationResult::PresetValidationResult(std::vector<PresetError> errors)
    : errors_(std::move(errors)) {}

PresetValidationResult validate_preset(const PresetDocument& preset) {
  std::vector<PresetError> errors;

  if (preset.schema_version == 0) {
    errors.push_back({"invalid_schema_version", "Preset schema version must be non-zero."});
  }

  if (preset.sample_rate == 0) {
    errors.push_back({"invalid_sample_rate", "Preset sample rate must be non-zero."});
  } else if (preset.sample_rate > kMaximumSampleRate) {
    errors.push_back({"sample_rate_too_large",
                      "Preset sample rate exceeds the supported control-plane limit."});
  }

  if (preset.frames_per_block == 0) {
    errors.push_back({"invalid_frames_per_block", "Preset frames per block must be non-zero."});
  } else if (preset.frames_per_block > kMaximumFramesPerBlock) {
    errors.push_back({"frames_per_block_too_large",
                      "Preset frames per block exceeds the supported control-plane limit."});
  }

  if (preset.nodes.size() > kMaximumNodes) {
    errors.push_back({"too_many_preset_nodes",
                      "Preset exceeds the supported node-count limit."});
  }
  if (preset.matrix.inputs.size() > kMaximumMatrixChannels) {
    errors.push_back({"too_many_matrix_inputs",
                      "Preset exceeds the supported matrix input-channel limit."});
  }
  if (preset.matrix.outputs.size() > kMaximumMatrixChannels) {
    errors.push_back({"too_many_matrix_outputs",
                      "Preset exceeds the supported matrix output-channel limit."});
  }
  if (preset.matrix.routes.size() > kMaximumRoutes) {
    errors.push_back({"too_many_matrix_routes",
                      "Preset exceeds the supported route-count limit."});
  }

  std::unordered_set<std::string> node_ids;
  for (const auto& node : preset.nodes) {
    validate_identifier(node.id, "empty_node_id", "Preset node IDs must not be empty.", errors);
    if (!node.id.empty() && !node_ids.insert(node.id).second) {
      errors.push_back({"duplicate_node_id", "Preset node IDs must be unique."});
    }
    validate_identifier(node.label,
                        "empty_node_label",
                        "Preset node labels must not be empty.",
                        errors);
    validate_identifier(node.type,
                        "empty_node_type",
                        "Preset node types must not be empty.",
                        errors);
  }

  std::unordered_set<std::string> input_ids;
  std::unordered_set<std::string> output_ids;
  validate_unique_endpoint_ids(preset.matrix.inputs,
                               "duplicate_input_endpoint_id",
                               "Input endpoint IDs must be unique.",
                               errors,
                               input_ids);
  validate_unique_endpoint_ids(preset.matrix.outputs,
                               "duplicate_output_endpoint_id",
                               "Output endpoint IDs must be unique.",
                               errors,
                               output_ids);

  if (preset.matrix.inputs.empty()) {
    errors.push_back({"empty_matrix_inputs", "Route matrix must contain at least one input."});
  }
  if (preset.matrix.outputs.empty()) {
    errors.push_back({"empty_matrix_outputs", "Route matrix must contain at least one output."});
  }

  std::unordered_set<std::string> route_pairs;
  for (const auto& route : preset.matrix.routes) {
    if (route.input_id.empty()) {
      errors.push_back({"empty_route_input", "Preset routes must reference an input endpoint."});
    } else if (!input_ids.contains(route.input_id)) {
      errors.push_back({"unknown_route_input", "Preset route references an unknown input endpoint."});
    }

    if (route.output_id.empty()) {
      errors.push_back({"empty_route_output", "Preset routes must reference an output endpoint."});
    } else if (!output_ids.contains(route.output_id)) {
      errors.push_back({
          "unknown_route_output",
          "Preset route references an unknown output endpoint.",
      });
    }

    if (!std::isfinite(route.gain)) {
      errors.push_back({"invalid_route_gain", "Preset route gain must be finite."});
    }

    if (!route.input_id.empty() && !route.output_id.empty()) {
      auto route_key = route.input_id;
      route_key.push_back('\n');
      route_key += route.output_id;
      if (!route_pairs.insert(std::move(route_key)).second) {
        errors.push_back({
            "duplicate_route",
            "Preset routes must not define the same crosspoint twice.",
        });
      }
    }
  }

  if (!errors.empty()) {
    return PresetValidationResult::failure(std::move(errors));
  }
  return PresetValidationResult::success();
}

PresetMatrixBuildResult PresetMatrixBuildResult::success(
    std::unique_ptr<graph::RouteMatrix> matrix) {
  return {std::move(matrix), {}};
}

PresetMatrixBuildResult PresetMatrixBuildResult::failure(std::vector<PresetError> errors) {
  return {nullptr, std::move(errors)};
}

bool PresetMatrixBuildResult::ok() const noexcept {
  return matrix_ != nullptr && errors_.empty();
}

graph::RouteMatrix* PresetMatrixBuildResult::matrix() const noexcept {
  return matrix_.get();
}

std::unique_ptr<graph::RouteMatrix> PresetMatrixBuildResult::take_matrix() noexcept {
  return std::move(matrix_);
}

const std::vector<PresetError>& PresetMatrixBuildResult::errors() const noexcept {
  return errors_;
}

PresetMatrixBuildResult::PresetMatrixBuildResult(std::unique_ptr<graph::RouteMatrix> matrix,
                                                 std::vector<PresetError> errors)
    : matrix_(std::move(matrix)), errors_(std::move(errors)) {}

PresetMatrixBuildResult build_route_matrix(const PresetDocument& preset) {
  auto validation = validate_preset(preset);
  if (!validation.ok()) {
    return PresetMatrixBuildResult::failure(validation.errors());
  }

  std::unordered_map<std::string, std::size_t> input_indices;
  std::unordered_map<std::string, std::size_t> output_indices;
  for (std::size_t index = 0; index < preset.matrix.inputs.size(); ++index) {
    input_indices.emplace(preset.matrix.inputs[index].id, index);
  }
  for (std::size_t index = 0; index < preset.matrix.outputs.size(); ++index) {
    output_indices.emplace(preset.matrix.outputs[index].id, index);
  }

  auto matrix = std::make_unique<graph::RouteMatrix>(preset.matrix.inputs,
                                                     preset.matrix.outputs);
  for (const auto& route : preset.matrix.routes) {
    if (route.muted) {
      continue;
    }
    static_cast<void>(matrix->set_gain(input_indices.at(route.input_id),
                                       output_indices.at(route.output_id),
                                       route.gain));
  }

  return PresetMatrixBuildResult::success(std::move(matrix));
}

PresetGraphBuildResult PresetGraphBuildResult::success(std::unique_ptr<graph::Graph> graph) {
  return {std::move(graph), {}};
}

PresetGraphBuildResult PresetGraphBuildResult::failure(std::vector<PresetError> errors) {
  return {nullptr, std::move(errors)};
}

bool PresetGraphBuildResult::ok() const noexcept {
  return graph_ != nullptr && errors_.empty();
}

graph::Graph* PresetGraphBuildResult::graph() const noexcept {
  return graph_.get();
}

std::unique_ptr<graph::Graph> PresetGraphBuildResult::take_graph() noexcept {
  return std::move(graph_);
}

const std::vector<PresetError>& PresetGraphBuildResult::errors() const noexcept {
  return errors_;
}

PresetGraphBuildResult::PresetGraphBuildResult(std::unique_ptr<graph::Graph> graph,
                                               std::vector<PresetError> errors)
    : graph_(std::move(graph)), errors_(std::move(errors)) {}

PresetGraphBuildResult build_preset_graph(const PresetDocument& preset,
                                          std::uint64_t graph_version) {
  auto validation = validate_preset(preset);
  if (!validation.ok()) {
    return PresetGraphBuildResult::failure(validation.errors());
  }

  std::vector<PresetError> errors;
  const auto* matrix_node = find_route_matrix_node(preset, errors);

  if (!errors.empty()) {
    return PresetGraphBuildResult::failure(std::move(errors));
  }

  auto matrix_result = build_route_matrix(preset);
  if (!matrix_result.ok()) {
    return PresetGraphBuildResult::failure(matrix_result.errors());
  }

  auto matrix = matrix_result.take_matrix();
  graph::GraphBuilder builder(graph_version,
                              std::max(matrix->input_channels(),
                                       matrix->output_channels()),
                              preset.frames_per_block);
  builder.sample_rate(preset.sample_rate)
      .add_node(matrix_node->id,
                matrix_node->label,
                std::make_unique<graph::RouteMatrixNode>(std::move(*matrix)));
  auto graph_result = builder.build();
  if (!graph_result.ok()) {
    append_graph_build_errors(graph_result, errors);
    return PresetGraphBuildResult::failure(std::move(errors));
  }

  return PresetGraphBuildResult::success(graph_result.take_graph());
}

}  // namespace sar::control
