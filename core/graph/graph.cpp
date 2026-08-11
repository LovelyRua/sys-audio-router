#include "core/graph/graph.h"

#include "core/graph/route_matrix.h"

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

bool GainNode::set_gain(float gain) noexcept {
  return gain_.set(gain);
}

float GainNode::gain() const noexcept {
  return gain_.get();
}

void GainNode::process(const realtime::ProcessContext& context,
                       const realtime::AudioBuffer& input,
                       realtime::AudioBuffer& output) noexcept {
  (void)context;
  const auto gain = gain_.get();
  const auto channels = std::min(input.channels(), output.channels());
  const auto frames = std::min(input.frames(), output.frames());

  for (std::size_t channel_index = 0; channel_index < channels; ++channel_index) {
    const auto source = input.channel(channel_index);
    auto destination = output.channel(channel_index);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      destination[frame] = source[frame] * gain;
    }
  }
}

MuteNode::MuteNode(bool muted) noexcept : muted_(muted ? 1U : 0U) {}

void MuteNode::set_muted(bool muted) noexcept {
  muted_.store(muted ? 1U : 0U, std::memory_order_relaxed);
}

bool MuteNode::muted() const noexcept {
  return muted_.load(std::memory_order_relaxed) != 0;
}

void MuteNode::process(const realtime::ProcessContext& context,
                       const realtime::AudioBuffer& input,
                       realtime::AudioBuffer& output) noexcept {
  (void)context;
  if (muted()) {
    output.clear();
    return;
  }
  output.copy_from(input);
}

void MeterNode::reset() noexcept {
  static_cast<void>(peak_.set(0.0F));
  static_cast<void>(rms_.set(0.0F));
}

float MeterNode::peak() const noexcept {
  return peak_.get();
}

float MeterNode::rms() const noexcept {
  return rms_.get();
}

void MeterNode::process(const realtime::ProcessContext& context,
                        const realtime::AudioBuffer& input,
                        realtime::AudioBuffer& output) noexcept {
  (void)context;
  output.copy_from(input);

  const auto channels = output.channels();
  const auto frames = output.frames();
  const auto sample_count = channels * frames;
  if (sample_count == 0) {
    reset();
    return;
  }

  float block_peak = 0.0F;
  double square_sum = 0.0;
  for (std::size_t channel = 0; channel < channels; ++channel) {
    const auto samples = output.channel(channel);
    for (const auto sample : samples) {
      if (!std::isfinite(sample)) {
        continue;
      }
      block_peak = std::max(block_peak, std::fabs(sample));
      square_sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
  }

  const auto block_rms = std::isfinite(square_sum)
                             ? static_cast<float>(std::sqrt(square_sum /
                                                             static_cast<double>(sample_count)))
                             : 0.0F;
  static_cast<void>(peak_.set(block_peak));
  static_cast<void>(rms_.set(std::isfinite(block_rms) ? block_rms : 0.0F));
}

BusNode::BusNode(std::size_t output_channels) noexcept
    : output_channels_(output_channels) {}

std::size_t BusNode::output_channels() const noexcept {
  return output_channels_;
}

void BusNode::process(const realtime::ProcessContext& context,
                      const realtime::AudioBuffer& input,
                      realtime::AudioBuffer& output) noexcept {
  (void)context;
  output.clear();

  const auto frames = std::min(input.frames(), output.frames());
  const auto input_channels = input.channels();
  const auto output_channels = std::min(output.channels(), output_channels_);
  if (output_channels == 0) {
    return;
  }

  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t out_channel = 0; out_channel < output_channels; ++out_channel) {
      float sum = 0.0F;
      for (std::size_t in_channel = out_channel; in_channel < input_channels;
           in_channel += output_channels) {
        sum += input.channel(in_channel)[frame];
      }
      output.channel(out_channel)[frame] = sum;
    }
  }
}

PolarityInvertNode::PolarityInvertNode(bool inverted) noexcept
    : inverted_(inverted ? 1U : 0U) {}

void PolarityInvertNode::set_inverted(bool inverted) noexcept {
  inverted_.store(inverted ? 1U : 0U, std::memory_order_relaxed);
}

bool PolarityInvertNode::inverted() const noexcept {
  return inverted_.load(std::memory_order_relaxed) != 0;
}

void PolarityInvertNode::process(const realtime::ProcessContext& context,
                                 const realtime::AudioBuffer& input,
                                 realtime::AudioBuffer& output) noexcept {
  (void)context;
  const auto channels = std::min(input.channels(), output.channels());
  const auto frames = std::min(input.frames(), output.frames());

  if (!inverted()) {
    output.copy_from(input);
    return;
  }

  for (std::size_t channel_index = 0; channel_index < channels; ++channel_index) {
    const auto source = input.channel(channel_index);
    auto destination = output.channel(channel_index);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      destination[frame] = -source[frame];
    }
  }
}

Graph::Graph(std::uint64_t version,
             std::size_t channels,
             std::size_t frames,
             std::uint32_t sample_rate)
    : version_(version),
      sample_rate_(sample_rate),
      channels_(channels),
      frames_(frames),
      scratch_a_(channels, frames),
      scratch_b_(channels, frames) {}

void Graph::add_node(std::unique_ptr<Node> node) {
  const auto default_name = "node_" + std::to_string(nodes_.size());
  add_node(default_name, default_name, std::move(node));
}

void Graph::add_node(std::string label, std::unique_ptr<Node> node) {
  add_node(label, label, std::move(node));
}

void Graph::add_node(std::string id, std::string label, std::unique_ptr<Node> node) {
  node_ids_.push_back(std::move(id));
  node_labels_.push_back(std::move(label));
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

  const auto block_seconds = static_cast<double>(input.frames()) /
                             static_cast<double>(sample_rate_);
  if (elapsed > block_seconds) {
    diagnostics.xrun_count += 1;
  }
}

std::uint64_t Graph::version() const noexcept {
  return version_;
}

std::size_t Graph::channels() const noexcept {
  return channels_;
}

std::size_t Graph::frames() const noexcept {
  return frames_;
}

std::uint32_t Graph::sample_rate() const noexcept {
  return sample_rate_;
}

std::size_t Graph::node_count() const noexcept {
  return nodes_.size();
}

std::string_view Graph::node_id(std::size_t index) const noexcept {
  if (index >= node_ids_.size()) {
    return {};
  }
  return node_ids_[index];
}

std::string_view Graph::node_label(std::size_t index) const noexcept {
  if (index >= node_labels_.size()) {
    return {};
  }
  return node_labels_[index];
}

bool Graph::apply_realtime_parameters_from(const Graph& source) noexcept {
  if (nodes_.size() != source.nodes_.size() ||
      node_ids_ != source.node_ids_) {
    return false;
  }
  for (std::size_t index = 0; index < nodes_.size(); ++index) {
    auto* target_matrix = dynamic_cast<RouteMatrixNode*>(nodes_[index].get());
    const auto* source_matrix =
        dynamic_cast<const RouteMatrixNode*>(source.nodes_[index].get());
    if ((target_matrix == nullptr) != (source_matrix == nullptr)) {
      return false;
    }
    if (target_matrix != nullptr &&
        !target_matrix->apply_realtime_parameters_from(*source_matrix)) {
      return false;
    }
  }
  return true;
}

}  // namespace sar::graph
