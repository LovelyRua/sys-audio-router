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

void add_legacy_port_ids(Direction direction,
                         std::unordered_set<std::string>& ids) {
  if (direction == Direction::Capture) {
    ids.insert("wasapi-capture-l");
    ids.insert("wasapi-capture-r");
  } else {
    ids.insert("wasapi-render-l");
    ids.insert("wasapi-render-r");
  }
}

void append_desired_runtime_ports(
    const control::AudioRuntimeConfiguration& configuration,
    Direction direction,
    std::vector<graph::RouteEndpointDescriptor>& ports) {
  if (configuration.mode != control::AudioRuntimeMode::WasapiMatrix) {
    if (direction == Direction::Capture) {
      ports.push_back({"wasapi-capture-l", "WASAPI Capture L"});
      ports.push_back({"wasapi-capture-r", "WASAPI Capture R"});
    } else {
      ports.push_back({"wasapi-render-l", "WASAPI Render L"});
      ports.push_back({"wasapi-render-r", "WASAPI Render R"});
    }
    return;
  }

  for (const auto& endpoint : configuration.endpoints) {
    if (endpoint.direction != direction) {
      continue;
    }
    for (std::uint32_t channel = 0; channel < endpoint.channel_count;
         ++channel) {
      ports.push_back({
          endpoint.endpoint_id + ".ch" + std::to_string(channel + 1),
          endpoint.endpoint_id + " Ch " + std::to_string(channel + 1),
      });
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
  if (next_configuration.mode == control::AudioRuntimeMode::WasapiMatrix) {
    add_runtime_port_ids(next_configuration, direction, desired);
  } else {
    add_legacy_port_ids(direction, desired);
  }

  std::vector<graph::RouteEndpointDescriptor> result;
  append_desired_runtime_ports(next_configuration, direction, result);
  for (const auto& port : current) {
    if (!removed.contains(port.id) && !desired.contains(port.id)) {
      result.push_back(port);
    }
  }
  return result;
}

void add_route_if_ports_exist(control::PresetDocument& preset,
                              const std::unordered_set<std::string>& inputs,
                              const std::unordered_set<std::string>& outputs,
                              std::string input_id,
                              std::string output_id) {
  if (!inputs.contains(input_id) || !outputs.contains(output_id)) {
    return;
  }
  const auto found = std::ranges::find_if(
      preset.matrix.routes, [&](const auto& route) {
        return route.input_id == input_id && route.output_id == output_id;
      });
  if (found == preset.matrix.routes.end()) {
    preset.matrix.routes.push_back(
        {std::move(input_id), std::move(output_id), 1.0F, false});
  }
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
  if (next_configuration.mode != control::AudioRuntimeMode::WasapiMatrix) {
    add_route_if_ports_exist(candidate, inputs, outputs, "asio-output-l",
                             "wasapi-render-l");
    add_route_if_ports_exist(candidate, inputs, outputs, "asio-output-r",
                             "wasapi-render-r");
    add_route_if_ports_exist(candidate, inputs, outputs, "wasapi-capture-l",
                             "asio-input-l");
    add_route_if_ports_exist(candidate, inputs, outputs, "wasapi-capture-r",
                             "asio-input-r");
  }

  const auto validation = control::validate_preset(candidate);
  if (!validation.ok()) {
    return AudioRuntimeMatrixPresetResult::failure(validation.errors());
  }
  return AudioRuntimeMatrixPresetResult::success(std::move(candidate));
}

}  // namespace sar::service
