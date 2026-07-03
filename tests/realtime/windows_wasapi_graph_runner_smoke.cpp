#include "core/platform/windows_wasapi_graph_runner.h"

#include "core/graph/node.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

bool has_error_code(const sar::platform::WasapiGraphRunnerResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiStreamProbe make_render_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = sar::platform::WasapiStreamDirection::Render;
  probe.device_id = "device";
  probe.device_label = "Device";
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.mix_format.frames_per_block = 4;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = 4;
  return probe;
}

sar::platform::WasapiStreamProbe make_capture_probe() {
  auto probe = make_render_probe();
  probe.direction = sar::platform::WasapiStreamDirection::Capture;
  return probe;
}

}  // namespace

int main() {
  {
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 2, 4);
    auto& input = runner.input_buffer();
    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      auto samples = input.channel(channel);
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        samples[frame] = static_cast<float>(frame + 1);
      }
    }

    sar::graph::Graph graph(7, 2, 4);
    graph.add_node(std::make_unique<sar::graph::GainNode>(0.5F));
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(result.ok(), "Expected graph-only runner success")) {
      return failure;
    }
    if (const auto failure = expect(result.stats().graph_processed,
                                    "Expected graph to be processed")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.processed_blocks == 1,
                                    "Expected diagnostics block count")) {
      return failure;
    }

    const auto& output = runner.output_buffer();
    if (const auto failure =
            expect(sar::tests::nearly_equal(output.channel(0)[2], 1.5F),
                   "Expected processed output sample")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 1, 4, 2, 8);
    auto& input = runner.input_buffer();
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      input.channel(0)[frame] = static_cast<float>(frame + 1);
    }

    sar::graph::Graph graph(10, 2, 8);
    graph.add_node(std::make_unique<sar::graph::GainNode>(0.25F));
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(result.ok(), "Expected mixed-shape runner success")) {
      return failure;
    }
    const auto& output = runner.output_buffer();
    if (const auto failure =
            expect(output.channels() == 2, "Expected render-shaped output channels")) {
      return failure;
    }
    if (const auto failure =
            expect(output.frames() == 8, "Expected render-shaped output frames")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(output.channel(0)[2], 0.75F),
                   "Expected mixed-shape processed output sample")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(output.channel(1)[2], 0.0F),
                   "Expected missing capture channel to remain silent")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream render_stream;
    auto open_result = render_stream.open(make_render_probe());
    if (const auto failure = expect(open_result.ok(), "Expected synthetic render open")) {
      return failure;
    }
    auto start_result = render_stream.start();
    if (const auto failure = expect(start_result.ok(), "Expected synthetic render start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 4);
    sar::graph::Graph graph(8, 2, 4);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(!result.ok(), "Expected synthetic render failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "native_stream_unavailable"),
                                    "Expected native_stream_unavailable")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream capture_stream;
    auto open_result = capture_stream.open(make_capture_probe());
    if (const auto failure = expect(open_result.ok(), "Expected synthetic capture open")) {
      return failure;
    }
    auto start_result = capture_stream.start();
    if (const auto failure = expect(start_result.ok(), "Expected synthetic capture start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, nullptr, 2, 4);
    auto& input = runner.input_buffer();
    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      auto samples = input.channel(channel);
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        samples[frame] = 1.0F;
      }
    }

    sar::graph::Graph graph(9, 2, 4);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(!result.ok(), "Expected synthetic capture failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "native_stream_unavailable"),
                                    "Expected capture native_stream_unavailable")) {
      return failure;
    }
    if (const auto failure = expect(input.channel(0)[0] == 0.0F,
                                    "Expected capture input to be cleared before pump")) {
      return failure;
    }
  }

  std::cout << "Windows WASAPI graph runner smoke test passed\n";
  return 0;
}
