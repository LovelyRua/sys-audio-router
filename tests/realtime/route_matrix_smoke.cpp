#include "core/graph/route_matrix.h"
#include "core/realtime/audio_buffer.h"
#include "core/realtime/process_context.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>

int main() {
  sar::realtime::AudioBuffer input(2, 4);
  sar::realtime::AudioBuffer output(2, 4);

  auto left = input.channel(0);
  auto right = input.channel(1);

  left[0] = 1.0F;
  left[1] = 2.0F;
  left[2] = 3.0F;
  left[3] = 4.0F;

  right[0] = 10.0F;
  right[1] = 20.0F;
  right[2] = 30.0F;
  right[3] = 40.0F;

  sar::graph::RouteMatrix matrix(2, 2);
  matrix.set_gain(0, 0, 1.0F);
  matrix.set_gain(1, 0, 0.5F);
  matrix.set_gain(1, 1, 1.0F);

  matrix.process(input, output);

  const auto out_left = output.channel(0);
  const auto out_right = output.channel(1);

  for (std::size_t frame = 0; frame < output.frames(); ++frame) {
    const auto expected_left = left[frame] + (right[frame] * 0.5F);
    const auto expected_right = right[frame];

    if (!sar::tests::nearly_equal(out_left[frame], expected_left)) {
      return sar::tests::fail_sample("Unexpected left matrix output", 0, frame);
    }

    if (!sar::tests::nearly_equal(out_right[frame], expected_right)) {
      return sar::tests::fail_sample("Unexpected right matrix output", 1, frame);
    }
  }

  std::cout << "Route matrix smoke test passed\n";
  return 0;
}
