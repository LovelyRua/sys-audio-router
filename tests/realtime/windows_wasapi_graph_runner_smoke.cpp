#include "core/platform/windows_wasapi_graph_runner.h"

#include "core/graph/node.h"
#include "core/graph/route_matrix.h"
#include "tests/realtime/scripted_wasapi_stream.h"
#include "tests/realtime/test_helpers.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

bool has_error_code(const sar::platform::WasapiGraphRunnerResult& result,
                    const std::string& code) {
  const auto realtime_error = result.realtime_error();
  if (realtime_error.code != 0 &&
      code == sar::platform::wasapi_realtime_error_code(realtime_error)) {
    return true;
  }
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

std::size_t error_code_count(const sar::platform::WasapiGraphRunnerResult& result,
                             const std::string& code) {
  std::size_t count = has_error_code(result, code) && result.realtime_error().code != 0
                          ? 1
                          : 0;
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      ++count;
    }
  }
  return count;
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

sar::platform::WasapiStreamProbe make_mismatched_capture_probe() {
  auto probe = make_capture_probe();
  probe.mix_format.sample_rate = 44100;
  return probe;
}

class EmptyRealtimeAudioSource final
    : public sar::platform::RealtimeAudioSource {
 public:
  [[nodiscard]] bool read(
      sar::realtime::AudioBuffer& destination) noexcept override {
    destination.clear();
    ++read_calls;
    return false;
  }

  std::size_t read_calls = 0;
};

class SingleBlockRealtimeAudioSource final
    : public sar::platform::RealtimeAudioSource {
 public:
  [[nodiscard]] bool read(
      sar::realtime::AudioBuffer& destination) noexcept override {
    destination.clear();
    if (consumed || destination.channels() < 2) {
      return false;
    }
    std::fill(destination.channel(0).begin(), destination.channel(0).end(),
              0.8F);
    std::fill(destination.channel(1).begin(), destination.channel(1).end(),
              -0.4F);
    consumed = true;
    return true;
  }

  bool consumed = false;
};

class RecordingRealtimeAudioSink final : public sar::platform::RealtimeAudioSink {
 public:
  [[nodiscard]] bool write(
      const sar::realtime::AudioBuffer& source) noexcept override {
    first_left = source.channel(0)[0];
    first_right = source.channel(1)[0];
    ++write_calls;
    return true;
  }

  float first_left = 0.0F;
  float first_right = 0.0F;
  std::size_t write_calls = 0;
};

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
    sar::tests::ScriptedWasapiStream capture_stream(make_capture_probe());
    capture_stream.enqueue_capture({
        .status = sar::platform::WasapiStreamIoStatus::Failed,
        .errors = {{"sample_channel_mismatch",
                    "Synthetic capture conversion failure."}},
    });
    sar::platform::WindowsWasapiGraphRunner runner(
        &capture_stream, nullptr, 2, 4);
    sar::graph::Graph graph(16, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;

    const auto result = runner.process_once(graph, diagnostics, 0);
    if (const auto failure =
            expect(!result.ok(), "Expected capture conversion failure")) {
      return failure;
    }
    if (const auto failure = expect(
            diagnostics.sample_conversion_import_failures == 1 &&
                diagnostics.sample_conversion_export_failures == 0,
            "Expected capture conversion diagnostics increment")) {
      return failure;
    }
  }

  {
    sar::tests::ScriptedWasapiStream render_stream(make_render_probe());
    render_stream.enqueue_render({
        .status = sar::platform::WasapiStreamIoStatus::Failed,
        .errors = {{"unsupported_sample_format",
                    "Synthetic render conversion failure."}},
    });
    sar::platform::WindowsWasapiGraphRunner runner(
        nullptr, &render_stream, 2, 4);
    sar::graph::Graph graph(17, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;

    const auto result = runner.process_once(graph, diagnostics, 0);
    if (const auto failure =
            expect(!result.ok(), "Expected render conversion failure")) {
      return failure;
    }
    if (const auto failure = expect(
            diagnostics.sample_conversion_import_failures == 0 &&
                diagnostics.sample_conversion_export_failures == 1,
            "Expected render conversion diagnostics increment")) {
      return failure;
    }
  }

  {
    sar::tests::ScriptedWasapiStream render_stream(make_render_probe());
    render_stream.enqueue_render({.writable_frames = 4});
    SingleBlockRealtimeAudioSource asio_output;
    RecordingRealtimeAudioSink asio_input;
    sar::platform::WasapiGraphChannelLayout layout{
        .graph_input_channels = 4,
        .graph_output_channels = 4,
        .capture_input_offset = 0,
        .external_input_offset = 2,
        .external_input_channels = 2,
        .render_output_offset = 0,
        .external_output_offset = 2,
        .external_output_channels = 2,
    };
    sar::platform::WindowsWasapiGraphRunner runner(
        nullptr, &render_stream, 2, 2, 4, 0, 4, 8, false, false,
        &asio_output, &asio_input, layout);

    sar::graph::RouteMatrix matrix(4, 4);
    static_cast<void>(matrix.set_gain(2, 0, 0.5F));
    static_cast<void>(matrix.set_gain(3, 1, 0.25F));
    static_cast<void>(matrix.set_gain(2, 2, 1.0F));
    static_cast<void>(matrix.set_gain(3, 3, 1.0F));
    sar::graph::Graph graph(19, 4, 4, 48000);
    graph.add_node(std::make_unique<sar::graph::RouteMatrixNode>(
        std::move(matrix)));
    sar::diagnostics::EngineDiagnostics diagnostics;

    const auto result = runner.process_once(graph, diagnostics, 10);
    if (const auto failure = expect(
            result.ok() && result.stats().graph_processed &&
                result.stats().external_input_mixed &&
                result.stats().external_output_published,
            "Expected mapped 4x4 graph processing success")) {
      return failure;
    }
    if (const auto failure = expect(
            render_stream.render_submissions().size() == 1 &&
                sar::tests::nearly_equal(
                    render_stream.render_submissions()[0].samples[0][0], 0.4F) &&
                sar::tests::nearly_equal(
                    render_stream.render_submissions()[0].samples[1][0], -0.1F),
            "Expected ASIO output routed to physical render channels")) {
      return failure;
    }
    if (const auto failure = expect(
            asio_input.write_calls == 1 &&
                sar::tests::nearly_equal(asio_input.first_left, 0.8F) &&
                sar::tests::nearly_equal(asio_input.first_right, -0.4F),
            "Expected graph output routed independently to ASIO input")) {
      return failure;
    }
  }

  {
    sar::tests::ScriptedWasapiStream render_stream(make_render_probe());
    render_stream.enqueue_render({.writable_frames = 4});
    render_stream.enqueue_render({.writable_frames = 4});
    EmptyRealtimeAudioSource external_input;
    sar::platform::WindowsWasapiGraphRunner runner(
        nullptr, &render_stream, 2, 2, 4, 0, 4, 8, false, false,
        &external_input);
    sar::graph::Graph graph(18, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;

    const auto first = runner.process_once(graph, diagnostics, 10);
    const auto second = runner.process_once(graph, diagnostics, 10);
    if (const auto failure =
            expect(first.ok() && second.ok(),
                   "Expected empty external input render pacing success")) {
      return failure;
    }
    if (const auto failure =
            expect(external_input.read_calls == 2,
                   "Expected one external input read per render cycle")) {
      return failure;
    }
    if (const auto failure =
            expect(render_stream.render_submissions().size() == 2,
                   "Expected empty external input to remain render paced")) {
      return failure;
    }
    for (const auto& submission : render_stream.render_submissions()) {
      if (const auto failure =
              expect(submission.frames == 4 &&
                         submission.samples[0][0] == 0.0F &&
                         submission.samples[1][0] == 0.0F,
                     "Expected render-paced silence while ASIO input is empty")) {
        return failure;
      }
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
    sar::platform::WindowsWasapiStream capture_stream;
    sar::platform::WindowsWasapiStream render_stream;
    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, &render_stream, 2, 4);
    const auto stop_result = runner.stop_streams();
    if (const auto failure =
            expect(!stop_result.ok(), "Expected closed duplex runner stop failure")) {
      return failure;
    }
    if (const auto failure =
            expect(error_code_count(stop_result, "stream_not_started") == 2,
                   "Expected closed duplex stop to aggregate both stream errors")) {
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
    sar::platform::WindowsWasapiGraphRunner runner(nullptr, &render_stream, 2, 4);
    const auto result = runner.stop_streams();

    if (const auto failure =
            expect(!result.ok(), "Expected closed render stop failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "stream_not_started"),
                                    "Expected stop_streams to report stream_not_started")) {
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
    auto open_result = capture_stream.open(make_mismatched_capture_probe());
    if (const auto failure =
            expect(open_result.ok(), "Expected mismatched synthetic capture open")) {
      return failure;
    }
    auto start_result = capture_stream.start();
    if (const auto failure =
            expect(start_result.ok(), "Expected mismatched synthetic capture start")) {
      return failure;
    }

    sar::platform::WindowsWasapiGraphRunner runner(&capture_stream, nullptr, 2, 4);
    auto& input = runner.input_buffer();
    for (std::size_t channel = 0; channel < input.channels(); ++channel) {
      auto samples = input.channel(channel);
      for (std::size_t frame = 0; frame < input.frames(); ++frame) {
        samples[frame] = 0.5F;
      }
    }

    sar::graph::Graph graph(13, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(!result.ok(),
                                    "Expected capture sample-rate mismatch failure")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_code(result, "graph_sample_rate_mismatch"),
                   "Expected capture graph_sample_rate_mismatch error")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.processed_blocks == 0,
                                    "Expected mismatched capture graph not to process")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(input.channel(0)[0], 0.5F),
                   "Expected capture sample-rate preflight not to clear input")) {
      return failure;
    }
  }

  {
    sar::tests::ScriptedWasapiStream capture_stream(make_mismatched_capture_probe());
    sar::tests::ScriptedWasapiStream render_stream(make_render_probe());
    for (std::size_t packet = 0; packet < 5; ++packet) {
      capture_stream.enqueue_capture({.frames = 64,
                                      .samples = {std::vector<float>(64, 0.5F),
                                                  std::vector<float>(64, -0.5F)}});
    }
    capture_stream.enqueue_capture(
        {.status = sar::platform::WasapiStreamIoStatus::Idle});
    render_stream.enqueue_render({.writable_frames = 64});
    render_stream.enqueue_render({.writable_frames = 64});

    sar::platform::WindowsWasapiGraphRunner runner(
        &capture_stream, &render_stream, 2, 2, 64, 64, 64, 256, true, true);
    sar::graph::Graph graph(14, 2, 64, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(result.ok(),
                                    "Expected native-rate adaptive capture success")) {
      return failure;
    }
    constexpr auto expected_nominal_ratio = 48000.0 / 44100.0;
    if (const auto failure = expect(
            result.stats().graph_processed &&
                result.stats().capture_rate_adapter_active &&
                std::abs(result.stats().capture_resampler_ratio -
                         expected_nominal_ratio) < 0.01,
            "Expected 48 kHz / 44.1 kHz adaptive capture output and ratio")) {
      return failure;
    }
  }

  {
    sar::tests::ScriptedWasapiStream capture_stream(make_mismatched_capture_probe());
    sar::tests::ScriptedWasapiStream render_stream(make_mismatched_render_probe());
    sar::platform::WindowsWasapiGraphRunner runner(
        &capture_stream, &render_stream, 2, 2, 64, 64, 64, 256, true, true);
    sar::graph::Graph graph(15, 2, 64, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result = runner.process_once(graph, diagnostics, 0);

    if (const auto failure = expect(!result.ok(),
                                    "Expected adaptive render sample-rate mismatch")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_code(result, "graph_sample_rate_mismatch"),
                   "Expected adaptive render graph_sample_rate_mismatch")) {
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
