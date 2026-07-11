#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <cmath>
#include <iostream>
#include <limits>

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
  sar::realtime::AudioBuffer input(2, 4);
  sar::realtime::AudioBuffer output(2, 4);

  input.channel(0)[0] = 1.0F;
  input.channel(0)[1] = -2.0F;
  input.channel(0)[2] = 0.0F;
  input.channel(0)[3] = 2.0F;
  input.channel(1)[0] = 0.5F;
  input.channel(1)[1] = -0.5F;
  input.channel(1)[2] = 1.5F;
  input.channel(1)[3] = -1.5F;

  sar::graph::MeterNode meter;
  sar::realtime::ProcessContext context{
      .sample_rate = 48000,
      .frames = input.frames(),
      .block_index = 0,
  };
  meter.process(context, input, output);

  if (const auto failure = expect(sar::tests::nearly_equal(meter.peak(), 2.0F),
                                  "Expected block peak")) {
    return failure;
  }
  if (const auto failure =
          expect(sar::tests::nearly_equal(meter.rms(), std::sqrt(1.75F)),
                 "Expected block RMS")) {
    return failure;
  }
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      if (!sar::tests::nearly_equal(input.channel(channel)[frame],
                                    output.channel(channel)[frame])) {
        return sar::tests::fail_sample("Meter node altered audio", channel, frame);
      }
    }
  }

  meter.reset();
  if (const auto failure = expect(meter.peak() == 0.0F && meter.rms() == 0.0F,
                                  "Expected meter reset")) {
    return failure;
  }

  input.channel(0)[0] = std::numeric_limits<float>::infinity();
  meter.process(context, input, output);
  if (const auto failure = expect(std::isfinite(meter.peak()) &&
                                      std::isfinite(meter.rms()),
                                  "Expected finite meter state for invalid input")) {
    return failure;
  }

  std::cout << "Meter node smoke test passed\n";
  return 0;
}
