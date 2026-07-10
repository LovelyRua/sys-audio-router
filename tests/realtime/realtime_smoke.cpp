#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <limits>
#include <memory>

namespace {

constexpr float kExpectedGain = 0.5F;
constexpr float kSecondGain = 0.25F;

}  // namespace

int main() {
  sar::realtime::AudioBuffer input(2, 128);
  sar::realtime::AudioBuffer output(2, 128);
  sar::diagnostics::EngineDiagnostics diagnostics;

  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = static_cast<float>(frame + 1);
    }
  }

  sar::graph::Graph graph(1, input.channels(), input.frames());
  graph.add_node(std::make_unique<sar::graph::GainNode>(kExpectedGain));
  graph.add_node(std::make_unique<sar::graph::GainNode>(kSecondGain));

  for (int block = 0; block < 1000; ++block) {
    graph.process(input, output, diagnostics);
  }

  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    const auto in = input.channel(channel);
    const auto out = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame) {
      if (!sar::tests::nearly_equal(out[frame], in[frame] * kExpectedGain * kSecondGain)) {
        return sar::tests::fail_sample("Unexpected sample", channel, frame);
      }
    }
  }

  if (diagnostics.graph_version != 1 || diagnostics.processed_blocks != 1000) {
    std::cerr << "Unexpected diagnostics state\n";
    return 1;
  }

  sar::graph::GainNode gain(0.75F);
  if (!gain.set_gain(0.25F)) {
    std::cerr << "Expected finite gain update to succeed\n";
    return 1;
  }
  if (gain.set_gain(std::numeric_limits<float>::quiet_NaN()) ||
      !sar::tests::nearly_equal(gain.gain(), 0.25F)) {
    std::cerr << "Expected non-finite gain update to preserve the active coefficient\n";
    return 1;
  }

  sar::graph::GainNode invalid_gain(std::numeric_limits<float>::infinity());
  if (!sar::tests::nearly_equal(invalid_gain.gain(), 0.0F)) {
    std::cerr << "Expected non-finite initial gain to become silence\n";
    return 1;
  }

  sar::graph::MuteNode mute(true);
  const sar::realtime::ProcessContext context{
      .sample_rate = 48000,
      .frames = input.frames(),
      .block_index = 0,
  };
  mute.process(context, input, output);
  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    for (const auto sample : output.channel(channel)) {
      if (!sar::tests::nearly_equal(sample, 0.0F)) {
        std::cerr << "Expected muted node to produce silence\n";
        return 1;
      }
    }
  }

  mute.set_muted(false);
  mute.process(context, input, output);
  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    const auto source = input.channel(channel);
    const auto destination = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame) {
      if (!sar::tests::nearly_equal(destination[frame], source[frame])) {
        std::cerr << "Expected unmuted node to copy input\n";
        return 1;
      }
    }
  }

  std::cout << "Realtime smoke test passed. peak_callback_seconds="
            << diagnostics.peak_callback_seconds << '\n';
  return 0;
}
