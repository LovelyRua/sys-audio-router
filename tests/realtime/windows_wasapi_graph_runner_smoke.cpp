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

sar::platform::WasapiStreamProbe make_mismatched_render_probe() {
  auto probe = make_render_probe();
  probe.mix_format.sample_rate = 44100;
  return probe;
}

}  // namespace

int main() {
  {
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 2, 4);
    const auto start_result = runner.start_streams();
    if (const auto failure =
            expect(start_result.ok(), "Expected graph-only stream start success")) {
      return failure;
    }
    const auto stop_result = runner.stop_streams();
    if (const auto failure =
            expect(stop_result.ok(), "Expected graph-only stream stop success")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream render_stream;
    auto open_result = render_stream.open(make_render_probe());
    if (const auto failure =
            expect(open_result.ok(), "Expected render stream open before runner start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 4);
    const auto start_result = runner.start_streams();
    if (const auto failure =
            expect(start_result.ok(), "Expected render stream runner start success")) {
      return failure;
    }
    if (const auto failure =
            expect(render_stream.state() == sar::platform::WasapiStreamState::Started,
                   "Expected render stream started by runner")) {
      return failure;
    }

    const auto stop_result = runner.stop_streams();
    if (const auto failure =
            expect(stop_result.ok(), "Expected render stream runner stop success")) {
      return failure;
    }
    if (const auto failure =
            expect(render_stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected render stream stopped by runner")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream capture_stream;
    auto open_result = capture_stream.open(make_capture_probe());
    if (const auto failure =
            expect(open_result.ok(), "Expected capture stream open before runner start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, nullptr, 2, 4);
    const auto start_result = runner.start_streams();
    if (const auto failure =
            expect(start_result.ok(), "Expected capture stream runner start success")) {
      return failure;
    }
    if (const auto failure =
            expect(capture_stream.state() == sar::platform::WasapiStreamState::Started,
                   "Expected capture stream started by runner")) {
      return failure;
    }

    const auto stop_result = runner.stop_streams();
    if (const auto failure =
            expect(stop_result.ok(), "Expected capture stream runner stop success")) {
      return failure;
    }
    if (const auto failure =
            expect(capture_stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected capture stream stopped by runner")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream capture_stream;
    auto capture_open_result = capture_stream.open(make_capture_probe());
    if (const auto failure =
            expect(capture_open_result.ok(), "Expected capture stream open before rollback")) {
      return failure;
    }
    sar::platform::WindowsWasapiStream render_stream;

    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, &render_stream, 2, 4);
    const auto start_result = runner.start_streams();
    if (const auto failure =
            expect(!start_result.ok(), "Expected render start failure after capture start")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(start_result, "stream_not_open"),
                                    "Expected stream_not_open from closed render stream")) {
      return failure;
    }
    if (const auto failure =
            expect(capture_stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected capture stream rollback after render start failure")) {
      return failure;
    }
    if (const auto failure =
            expect(render_stream.state() == sar::platform::WasapiStreamState::Closed,
                   "Expected render stream to remain closed after start failure")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream capture_stream;
    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, nullptr, 2, 4);
    const auto start_result = runner.start_streams();
    if (const auto failure =
            expect(!start_result.ok(), "Expected closed capture runner start failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(start_result, "stream_not_open"),
                                    "Expected stream_not_open from closed capture stream")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream render_stream;
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 4);
    const auto stop_result = runner.stop_streams();
    if (const auto failure =
            expect(!stop_result.ok(), "Expected closed render runner stop failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(stop_result, "stream_not_started"),
                                    "Expected stream_not_started from closed render stop")) {
      return failure;
    }
  }

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
    if (const auto failure = expect(!result.stats().capture_partial,
                                    "Expected graph-only runner without partial capture")) {
      return failure;
    }
    if (const auto failure = expect(!result.stats().render_partial,
                                    "Expected graph-only runner without partial render")) {
      return failure;
    }
    if (const auto failure = expect(result.stats().capture_partial_frames == 0,
                                    "Expected graph-only runner without partial capture frames")) {
      return failure;
    }
    if (const auto failure = expect(result.stats().render_partial_frames == 0,
                                    "Expected graph-only runner without partial render frames")) {
      return failure;
    }
    if (const auto failure = expect(!result.stats().capture_silent,
                                    "Expected graph-only runner without silent capture")) {
      return failure;
    }
    if (const auto failure = expect(result.stats().capture_silent_frames == 0,
                                    "Expected graph-only runner without silent capture frames")) {
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
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, nullptr, 2, 4);
    sar::graph::Graph graph(11, 1, 4);
    graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
    graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure =
            expect(!result.ok(), "Expected undersized multi-node graph failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "graph_buffer_too_small"),
                                    "Expected graph_buffer_too_small error")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.processed_blocks == 0,
                                    "Expected undersized graph not to process")) {
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
    sar::platform::WindowsWasapiStream render_stream;
    auto open_result = render_stream.open(make_mismatched_render_probe());
    if (const auto failure =
            expect(open_result.ok(), "Expected mismatched synthetic render open")) {
      return failure;
    }
    auto start_result = render_stream.start();
    if (const auto failure =
            expect(start_result.ok(), "Expected mismatched synthetic render start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 4);
    sar::graph::Graph graph(12, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(!result.ok(),
                                    "Expected graph sample-rate mismatch failure")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_code(result, "graph_sample_rate_mismatch"),
                   "Expected graph_sample_rate_mismatch error")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.processed_blocks == 0,
                                    "Expected mismatched graph not to process")) {
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
