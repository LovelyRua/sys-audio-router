#include "core/graph/route_matrix.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace sar::graph {

namespace {

std::string to_string(std::string_view value) {
  return {value.data(), value.size()};
}

std::vector<RouteEndpointDescriptor> make_default_endpoints(std::size_t count,
                                                            std::string_view id_prefix,
                                                            std::string_view label_prefix) {
  std::vector<RouteEndpointDescriptor> endpoints;
  endpoints.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    endpoints.push_back({
        to_string(id_prefix) + "_" + std::to_string(index),
        to_string(label_prefix) + " " + std::to_string(index + 1),
    });
  }
  return endpoints;
}

void validate_endpoints(const std::vector<RouteEndpointDescriptor>& endpoints,
                        std::string_view side) {
  if (endpoints.empty()) {
    throw std::invalid_argument("RouteMatrix requires at least one " + to_string(side) +
                                " endpoint");
  }

  std::unordered_set<std::string> ids;
  for (const auto& endpoint : endpoints) {
    if (endpoint.id.empty()) {
      throw std::invalid_argument("RouteMatrix " + to_string(side) +
                                  " endpoint IDs must not be empty");
    }
    if (endpoint.label.empty()) {
      throw std::invalid_argument("RouteMatrix " + to_string(side) +
                                  " endpoint labels must not be empty");
    }
    if (!ids.insert(endpoint.id).second) {
      throw std::invalid_argument("RouteMatrix " + to_string(side) +
                                  " endpoint IDs must be unique");
    }
  }
}

}  // namespace

RouteMatrix::RouteMatrix(std::size_t input_channels, std::size_t output_channels)
    : RouteMatrix(make_default_endpoints(input_channels, "in", "Input"),
                  make_default_endpoints(output_channels, "out", "Output")) {}

RouteMatrix::RouteMatrix(std::vector<RouteEndpointDescriptor> inputs,
                         std::vector<RouteEndpointDescriptor> outputs)
    : input_channels_(inputs.size()),
      output_channels_(outputs.size()),
      inputs_(std::move(inputs)),
      outputs_(std::move(outputs)),
      gains_(input_channels_ * output_channels_, 0.0F) {
  validate_endpoints(inputs_, "input");
  validate_endpoints(outputs_, "output");
}

std::size_t RouteMatrix::input_channels() const noexcept {
  return input_channels_;
}

std::size_t RouteMatrix::output_channels() const noexcept {
  return output_channels_;
}

std::string_view RouteMatrix::input_id(std::size_t input_channel) const noexcept {
  if (input_channel >= inputs_.size()) {
    return {};
  }
  return inputs_[input_channel].id;
}

std::string_view RouteMatrix::input_label(std::size_t input_channel) const noexcept {
  if (input_channel >= inputs_.size()) {
    return {};
  }
  return inputs_[input_channel].label;
}

std::string_view RouteMatrix::output_id(std::size_t output_channel) const noexcept {
  if (output_channel >= outputs_.size()) {
    return {};
  }
  return outputs_[output_channel].id;
}

std::string_view RouteMatrix::output_label(std::size_t output_channel) const noexcept {
  if (output_channel >= outputs_.size()) {
    return {};
  }
  return outputs_[output_channel].label;
}

void RouteMatrix::clear_routes() noexcept {
  for (auto& gain : gains_) {
    std::atomic_ref<float>(gain).store(0.0F, std::memory_order_release);
  }
}

bool RouteMatrix::set_gain(std::size_t input_channel,
                           std::size_t output_channel,
                           float gain) noexcept {
  if (input_channel >= input_channels_ || output_channel >= output_channels_ ||
      !std::isfinite(gain)) {
    return false;
  }

  std::atomic_ref<float>(gains_[index(input_channel, output_channel)])
      .store(gain, std::memory_order_release);
  return true;
}

float RouteMatrix::gain(std::size_t input_channel,
                        std::size_t output_channel) const noexcept {
  if (input_channel >= input_channels_ || output_channel >= output_channels_) {
    return 0.0F;
  }

  return std::atomic_ref<const float>(
             gains_[index(input_channel, output_channel)])
      .load(std::memory_order_acquire);
}

bool RouteMatrix::copy_gains_from(const RouteMatrix& source) noexcept {
  if (input_channels_ != source.input_channels_ ||
      output_channels_ != source.output_channels_) {
    return false;
  }
  for (std::size_t input = 0; input < input_channels_; ++input) {
    if (input_id(input) != source.input_id(input)) {
      return false;
    }
  }
  for (std::size_t output = 0; output < output_channels_; ++output) {
    if (output_id(output) != source.output_id(output)) {
      return false;
    }
  }
  for (std::size_t output = 0; output < output_channels_; ++output) {
    for (std::size_t input = 0; input < input_channels_; ++input) {
      static_cast<void>(set_gain(input, output, source.gain(input, output)));
    }
  }
  return true;
}

void RouteMatrix::process(const realtime::AudioBuffer& input,
                          realtime::AudioBuffer& output) const noexcept {
  output.clear();

  const auto inputs = std::min(input.channels(), input_channels_);
  const auto outputs = std::min(output.channels(), output_channels_);
  const auto frames = std::min(input.frames(), output.frames());

  for (std::size_t output_channel = 0; output_channel < outputs; ++output_channel) {
    auto out = output.channel(output_channel);

    for (std::size_t input_channel = 0; input_channel < inputs; ++input_channel) {
      const auto route_gain = gain(input_channel, output_channel);
      if (route_gain == 0.0F) {
        continue;
      }

      const auto in = input.channel(input_channel);
      for (std::size_t frame = 0; frame < frames; ++frame) {
        out[frame] += in[frame] * route_gain;
      }
    }
  }
}

std::size_t RouteMatrix::index(std::size_t input_channel,
                               std::size_t output_channel) const noexcept {
  return output_channel * input_channels_ + input_channel;
}

RouteMatrixNode::RouteMatrixNode(RouteMatrix matrix) noexcept
    : matrix_(std::move(matrix)) {}

const RouteMatrix& RouteMatrixNode::matrix() const noexcept {
  return matrix_;
}

bool RouteMatrixNode::apply_realtime_parameters_from(
    const RouteMatrixNode& source) noexcept {
  return matrix_.copy_gains_from(source.matrix_);
}

void RouteMatrixNode::process(const realtime::ProcessContext& context,
                              const realtime::AudioBuffer& input,
                              realtime::AudioBuffer& output) noexcept {
  (void)context;
  matrix_.process(input, output);
}

}  // namespace sar::graph
