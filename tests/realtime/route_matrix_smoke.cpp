#include "core/graph/route_matrix.h"
#include "core/realtime/audio_buffer.h"
#include "core/realtime/process_context.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

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
  if (const auto failure = expect(matrix.input_id(0) == std::string_view{"in_0"},
                                  "Expected default input ID")) {
    return failure;
  }
  if (const auto failure = expect(matrix.input_label(0) == std::string_view{"Input 1"},
                                  "Expected default input label")) {
    return failure;
  }
  if (const auto failure = expect(matrix.output_id(1) == std::string_view{"out_1"},
                                  "Expected default output ID")) {
    return failure;
  }
  if (const auto failure = expect(matrix.output_label(1) == std::string_view{"Output 2"},
                                  "Expected default output label")) {
    return failure;
  }

  if (const auto failure = expect(matrix.set_gain(0, 0, 1.0F) &&
                                  matrix.set_gain(1, 0, 0.5F) &&
                                  matrix.set_gain(1, 1, 1.0F),
                                  "Expected finite route gains to be accepted")) {
    return failure;
  }
  if (const auto failure =
          expect(!matrix.set_gain(0, 0, std::numeric_limits<float>::quiet_NaN()) &&
                     !matrix.set_gain(2, 0, 1.0F) &&
                     sar::tests::nearly_equal(matrix.gain(0, 0), 1.0F),
                 "Expected invalid route gains to preserve the active crosspoint")) {
    return failure;
  }

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

  if (const auto failure = expect(!matrix.set_gain(0, 1,
                                                    std::numeric_limits<float>::infinity()) &&
                                  sar::tests::nearly_equal(matrix.gain(0, 1), 0.0F),
                                  "Expected infinite route gain to be rejected")) {
    return failure;
  }

  {
    sar::graph::RouteMatrix named_matrix(
        {
            {"mic_left", "Mic Left"},
            {"mic_right", "Mic Right"},
        },
        {
            {"monitor_left", "Monitor Left"},
            {"monitor_right", "Monitor Right"},
        });

    if (const auto failure = expect(named_matrix.input_id(1) == std::string_view{"mic_right"},
                                    "Expected named input ID")) {
      return failure;
    }
    if (const auto failure =
            expect(named_matrix.output_label(0) == std::string_view{"Monitor Left"},
                   "Expected named output label")) {
      return failure;
    }
  }

  try {
    sar::graph::RouteMatrix invalid_matrix(
        {
            {"dup", "Input A"},
            {"dup", "Input B"},
        },
        {
            {"out", "Output"},
        });
    (void)invalid_matrix;
    std::cerr << "Expected duplicate endpoint ID failure\n";
    return 1;
  } catch (const std::invalid_argument&) {
  }

  std::cout << "Route matrix smoke test passed\n";
  return 0;
}
