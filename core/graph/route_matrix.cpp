#include "core/graph/route_matrix.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sar::graph {

RouteMatrix::RouteMatrix(std::size_t input_channels, std::size_t output_channels)
    : input_channels_(input_channels),
      output_channels_(output_channels),
      gains_(input_channels * output_channels, 0.0F) {
  if (input_channels == 0 || output_channels == 0) {
    throw std::invalid_argument("RouteMatrix requires non-zero input and output channels");
  }
}

std::size_t RouteMatrix::input_channels() const noexcept {
  return input_channels_;
}

std::size_t RouteMatrix::output_channels() const noexcept {
  return output_channels_;
}

void RouteMatrix::clear_routes() noexcept {
  std::ranges::fill(gains_, 0.0F);
}

void RouteMatrix::set_gain(std::size_t input_channel,
                           std::size_t output_channel,
                           float gain) noexcept {
  if (input_channel >= input_channels_ || output_channel >= output_channels_) {
    return;
  }

  gains_[index(input_channel, output_channel)] = gain;
}

float RouteMatrix::gain(std::size_t input_channel,
                        std::size_t output_channel) const noexcept {
  if (input_channel >= input_channels_ || output_channel >= output_channels_) {
    return 0.0F;
  }

  return gains_[index(input_channel, output_channel)];
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

void RouteMatrixNode::process(const realtime::ProcessContext& context,
                              const realtime::AudioBuffer& input,
                              realtime::AudioBuffer& output) noexcept {
  (void)context;
  matrix_.process(input, output);
}

}  // namespace sar::graph
