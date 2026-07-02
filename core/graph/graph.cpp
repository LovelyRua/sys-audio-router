#include "core/graph/graph.h"

#include <algorithm>
#include <chrono>

namespace sar::graph {

void PassthroughNode::process(const realtime::ProcessContext& context,
                              const realtime::AudioBuffer& input,
                              realtime::AudioBuffer& output) noexcept {
  (void)context;
  const auto channels = std::min(input.channels(), output.channels());
  const auto frames = std::min(input.frames(), output.frames());

  for (std::size_t channel_index = 0; channel_index < channels; ++channel_index) {
    const auto source = input.channel(channel_index);
    auto destination = output.channel(channel_index);
    std::copy_n(source.begin(), frames, destination.begin());
  }
}

GainNode::GainNode(float gain) noexcept : gain_(gain) {}

void GainNode::set_gain(float gain) noexcept {
  gain_ = gain;
}

float GainNode::gain() const noexcept {
  return gain_;
}

void GainNode::process(const realtime::ProcessContext& context,
                       const realtime::AudioBuffer& input,
                       realtime::AudioBuffer& output) noexcept {
  (void)context;
  const auto channels = std::min(input.channels(), output.channels());
  const auto frames = std::min(input.frames(), output.frames());

  for (std::size_t channel_index = 0; channel_index < channels; ++channel_index) {
    const auto source = input.channel(channel_index);
    auto destination = output.channel(channel_index);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      destination[frame] = source[frame] * gain_;
    }
  }
}

Graph::Graph(std::uint64_t version,
             std::size_t channels,
             std::size_t frames,
             std::uint32_t sample_rate)
    : version_(version),
      sample_rate_(sample_rate),
      scratch_a_(channels, frames),
      scratch_b_(channels, frames) {}

void Graph::add_node(std::unique_ptr<Node> node) {
  nodes_.push_back(std::move(node));
}

void Graph::process(const realtime::AudioBuffer& input,
                    realtime::AudioBuffer& output,
                    diagnostics::EngineDiagnostics& diagnostics) noexcept {
  const auto started = std::chrono::steady_clock::now();

  if (nodes_.empty()) {
    output.copy_from(input);
  } else {
    const realtime::ProcessContext context{
        .sample_rate = sample_rate_,
        .frames = input.frames(),
        .block_index = diagnostics.processed_blocks,
    };
    const realtime::AudioBuffer* current_input = &input;
    realtime::AudioBuffer* current_output = nullptr;

    for (std::size_t node_index = 0; node_index < nodes_.size(); ++node_index) {
      const auto is_last = node_index == nodes_.size() - 1;
      if (is_last) {
        current_output = &output;
      } else {
        current_output = (node_index % 2 == 0) ? &scratch_a_ : &scratch_b_;
      }

      current_output->clear();
      nodes_[node_index]->process(context, *current_input, *current_output);
      current_input = current_output;
    }
  }

  const auto ended = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration<double>(ended - started).count();

  diagnostics.graph_version = version_;
  diagnostics.processed_blocks += 1;
  diagnostics.last_callback_seconds = elapsed;
  diagnostics.peak_callback_seconds = std::max(diagnostics.peak_callback_seconds, elapsed);
}

std::uint64_t Graph::version() const noexcept {
  return version_;
}

std::size_t Graph::node_count() const noexcept {
  return nodes_.size();
}

}  // namespace sar::graph
