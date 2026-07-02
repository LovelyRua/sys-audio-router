#pragma once

#include "core/graph/route_matrix.h"

#include <cstdint>
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

[[nodiscard]] PresetValidationResult validate_preset(const PresetDocument& preset);

}  // namespace sar::control
