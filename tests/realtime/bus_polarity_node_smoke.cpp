#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  // --- BusNode: stereo-to-mono downmix ---
  {
    sar::realtime::AudioBuffer input(2, 4);
    sar::realtime::AudioBuffer output(1, 4);

    input.channel(0)[0] = 1.0F;
    input.channel(0)[1] = -2.0F;
    input.channel(0)[2] = 0.5F;
    input.channel(0)[3] = 0.25F;
    input.channel(1)[0] = 0.5F;
    input.channel(1)[1] = 2.0F;
    input.channel(1)[2] = 0.5F;
    input.channel(1)[3] = 0.75F;

    sar::graph::BusNode bus(1);
    sar::realtime::ProcessContext context{
        .sample_rate = 48000,
        .frames = input.frames(),
        .block_index = 0,
    };
    bus.process(context, input, output);

    if (const auto failure = expect(bus.output_channels() == 1,
                                    "Expected BusNode output channels 1")) {
      return failure;
    }
    const auto out = output.channel(0);
    if (const auto failure = expect(sar::tests::nearly_equal(out[0], 1.5F),
                                    "Expected downmix frame 0")) {
      return failure;
    }
    if (const auto failure = expect(sar::tests::nearly_equal(out[1], 0.0F),
                                    "Expected downmix frame 1")) {
      return failure;
    }
    if (const auto failure = expect(sar::tests::nearly_equal(out[2], 1.0F),
                                    "Expected downmix frame 2")) {
      return failure;
    }
    if (const auto failure = expect(sar::tests::nearly_equal(out[3], 1.0F),
                                    "Expected downmix frame 3")) {
      return failure;
    }
  }

  // --- BusNode: mono-to-stereo upmix (extra outputs silenced) ---
  {
    sar::realtime::AudioBuffer input(1, 2);
    sar::realtime::AudioBuffer output(2, 2);

    input.channel(0)[0] = 0.5F;
    input.channel(0)[1] = -0.25F;

    sar::graph::BusNode bus(2);
    sar::realtime::ProcessContext context{
        .sample_rate = 48000,
        .frames = input.frames(),
        .block_index = 0,
    };
    bus.process(context, input, output);

    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      if (!sar::tests::nearly_equal(input.channel(0)[frame],
                                    output.channel(0)[frame])) {
        return sar::tests::fail_sample("BusNode upmix altered channel 0",
                                       0, frame);
      }
      if (!sar::tests::nearly_equal(output.channel(1)[frame], 0.0F)) {
        return sar::tests::fail_sample("BusNode upmix left channel 1 non-silent",
                                       1, frame);
      }
    }
  }

  // --- BusNode: zero configured outputs returns silence without hanging ---
  {
    sar::realtime::AudioBuffer input(1, 2);
    sar::realtime::AudioBuffer output(2, 2);
    input.channel(0)[0] = 0.5F;
    input.channel(0)[1] = -0.25F;
    output.channel(0)[0] = 1.0F;
    output.channel(1)[1] = 1.0F;

    sar::graph::BusNode bus(0);
    sar::realtime::ProcessContext context{
        .sample_rate = 48000,
        .frames = input.frames(),
        .block_index = 0,
    };
    bus.process(context, input, output);

    for (std::size_t channel = 0; channel < output.channels(); ++channel) {
      for (std::size_t frame = 0; frame < output.frames(); ++frame) {
        if (!sar::tests::nearly_equal(output.channel(channel)[frame], 0.0F)) {
          return sar::tests::fail_sample(
              "BusNode with zero outputs did not return silence", channel, frame);
        }
      }
    }
  }

  // --- PolarityInvertNode: inversion enabled (default) ---
  {
    sar::realtime::AudioBuffer input(2, 4);
    sar::realtime::AudioBuffer output(2, 4);

    input.channel(0)[0] = 1.0F;
    input.channel(0)[1] = -2.0F;
    input.channel(0)[2] = 0.0F;
    input.channel(0)[3] = 0.5F;
    input.channel(1)[0] = -0.5F;
    input.channel(1)[1] = 0.25F;
    input.channel(1)[2] = 1.5F;
    input.channel(1)[3] = -1.5F;

    sar::graph::PolarityInvertNode invert;
    sar::realtime::ProcessContext context{
        .sample_rate = 48000,
        .frames = input.frames(),
        .block_index = 0,
    };
    invert.process(context, input, output);

    if (const auto failure = expect(invert.inverted(),
                                    "Expected PolarityInvertNode inverted by default")) {
      return failure;
    }
    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        if (!sar::tests::nearly_equal(output.channel(channel)[frame],
                                      -input.channel(channel)[frame])) {
          return sar::tests::fail_sample("PolarityInvertNode did not negate",
                                         channel, frame);
        }
      }
    }
  }

  // --- PolarityInvertNode: inversion disabled (passthrough) ---
  {
    sar::realtime::AudioBuffer input(1, 3);
    sar::realtime::AudioBuffer output(1, 3);

    input.channel(0)[0] = 0.5F;
    input.channel(0)[1] = -0.5F;
    input.channel(0)[2] = 1.0F;

    sar::graph::PolarityInvertNode invert(false);
    sar::realtime::ProcessContext context{
        .sample_rate = 48000,
        .frames = input.frames(),
        .block_index = 0,
    };
    invert.process(context, input, output);

    if (const auto failure = expect(!invert.inverted(),
                                    "Expected PolarityInvertNode not inverted")) {
      return failure;
    }
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      if (!sar::tests::nearly_equal(input.channel(0)[frame],
                                    output.channel(0)[frame])) {
        return sar::tests::fail_sample("PolarityInvertNode altered audio when disabled",
                                       0, frame);
      }
    }

    invert.set_inverted(true);
    invert.process(context, input, output);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      if (!sar::tests::nearly_equal(output.channel(0)[frame],
                                    -input.channel(0)[frame])) {
        return sar::tests::fail_sample("PolarityInvertNode did not negate after toggle",
                                       0, frame);
      }
    }
  }

  std::cout << "Bus and polarity invert node smoke test passed\n";
  return 0;
}
