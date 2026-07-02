#include "core/control/preset_document.h"

#include <cmath>
#include <unordered_set>
#include <utility>

namespace sar::control {

namespace {

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
  }

  if (preset.frames_per_block == 0) {
    errors.push_back({"invalid_frames_per_block", "Preset frames per block must be non-zero."});
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
  }

  if (!errors.empty()) {
    return PresetValidationResult::failure(std::move(errors));
  }
  return PresetValidationResult::success();
}

}  // namespace sar::control
