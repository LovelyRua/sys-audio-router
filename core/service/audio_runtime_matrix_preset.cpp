#include "core/service/audio_runtime_matrix_preset.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace sar::service {

namespace {

using Direction = control::AudioRuntimeEndpointDirection;

void add_runtime_port_ids(
    const control::AudioRuntimeConfiguration& configuration,
    Direction direction,
    std::unordered_set<std::string>& ids) {
  if (configuration.mode != control::AudioRuntimeMode::WasapiMatrix) {
    return;
  }
  for (const auto& endpoint : configuration.endpoints) {
    if (endpoint.direction != direction) {
      continue;
    }
    for (std::uint32_t channel = 0; channel < endpoint.channel_count;
         ++channel) {
      ids.insert(endpoint.endpoint_id + ".ch" +
                 std::to_string(channel + 1));
    }
  }
}

std::vector<graph::RouteEndpointDescriptor> reconcile_axis(
    const std::vector<graph::RouteEndpointDescriptor>& current,
    const control::AudioRuntimeConfiguration& previous_configuration,
    const control::AudioRuntimeConfiguration& next_configuration,
    Direction direction) {
  std::unordered_set<std::string> removed;
  add_runtime_port_ids(previous_configuration, direction, removed);
  if (direction == Direction::Capture) {
    removed.insert("wasapi-capture-l");
    removed.insert("wasapi-capture-r");
  } else {
    removed.insert("wasapi-render-l");
    removed.insert("wasapi-render-r");
  }

  std::unordered_set<std::string> desired;
  add_runtime_port_ids(next_configuration, direction, desired);

  std::vector<graph::RouteEndpointDescriptor> result;
  for (const auto& endpoint : next_configuration.endpoints) {
    if (endpoint.direction != direction) {
      continue;
    }
    for (std::uint32_t channel = 0; channel < endpoint.channel_count;
         ++channel) {
      result.push_back({
          endpoint.endpoint_id + ".ch" + std::to_string(channel + 1),
          endpoint.endpoint_id + " Ch " + std::to_string(channel + 1),
      });
    }
  }
  for (const auto& port : current) {
    if (!removed.contains(port.id) && !desired.contains(port.id)) {
      result.push_back(port);
    }
  }
  return result;
}

std::unordered_set<std::string> port_ids(
    const std::vector<graph::RouteEndpointDescriptor>& ports) {
  std::unordered_set<std::string> result;
  result.reserve(ports.size());
  for (const auto& port : ports) {
    result.insert(port.id);
  }
  return result;
}

}  // namespace

AudioRuntimeMatrixPresetResult AudioRuntimeMatrixPresetResult::success(
    control::PresetDocument preset) {
  return {std::move(preset), {}};
}

AudioRuntimeMatrixPresetResult AudioRuntimeMatrixPresetResult::failure(
    std::vector<control::PresetError> errors) {
  return {{}, std::move(errors)};
}

bool AudioRuntimeMatrixPresetResult::ok() const noexcept {
  return errors_.empty();
}

const control::PresetDocument& AudioRuntimeMatrixPresetResult::preset()
    const noexcept {
  return preset_;
}

control::PresetDocument AudioRuntimeMatrixPresetResult::take_preset() noexcept {
  return std::move(preset_);
}

const std::vector<control::PresetError>&
AudioRuntimeMatrixPresetResult::errors() const noexcept {
  return errors_;
}

AudioRuntimeMatrixPresetResult::AudioRuntimeMatrixPresetResult(
    control::PresetDocument preset,
    std::vector<control::PresetError> errors) noexcept
    : preset_(std::move(preset)), errors_(std::move(errors)) {}

AudioRuntimeMatrixPresetResult reconcile_audio_runtime_matrix_preset(
    const control::PresetDocument& current,
    const control::AudioRuntimeConfiguration& previous_configuration,
    const control::AudioRuntimeConfiguration& next_configuration) {
  auto configuration_errors =
      control::validate_audio_runtime_configuration(next_configuration, false);
  if (!configuration_errors.empty()) {
    return AudioRuntimeMatrixPresetResult::failure(
        std::move(configuration_errors));
  }
  if (next_configuration.mode != control::AudioRuntimeMode::WasapiMatrix) {
    return AudioRuntimeMatrixPresetResult::failure({{
        "audio_runtime_matrix_preset_requires_matrix_mode",
        "Runtime matrix preset reconciliation requires WASAPI matrix mode.",
    }});
  }

  auto candidate = current;
  candidate.matrix.inputs = reconcile_axis(
      current.matrix.inputs, previous_configuration, next_configuration,
      Direction::Capture);
  candidate.matrix.outputs = reconcile_axis(
      current.matrix.outputs, previous_configuration, next_configuration,
      Direction::Render);

  const auto inputs = port_ids(candidate.matrix.inputs);
  const auto outputs = port_ids(candidate.matrix.outputs);
  std::erase_if(candidate.matrix.routes, [&](const auto& route) {
    return !inputs.contains(route.input_id) ||
           !outputs.contains(route.output_id);
  });

  const auto validation = control::validate_preset(candidate);
  if (!validation.ok()) {
    return AudioRuntimeMatrixPresetResult::failure(validation.errors());
  }
  return AudioRuntimeMatrixPresetResult::success(std::move(candidate));
}

}  // namespace sar::service
