#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include "core/graph/node.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool has_default_output_device() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return false;
  }

  for (const auto& device : result.devices()) {
    if (device.direction == sar::platform::AudioDeviceDirection::Output &&
        device.is_default) {
      return true;
    }
  }
  return false;
}

bool has_error_code(const sar::platform::WasapiRenderLoopOpenResult& result,
                    const char* code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

std::uint32_t mismatched_sample_rate(std::uint32_t sample_rate) noexcept {
  return sample_rate == 48000 ? 44100 : 48000;
}

}  // namespace

int main() {
  if (!has_default_output_device()) {
    std::cout << "Windows WASAPI render loop skipped: no default output endpoint\n";
    return 0;
  }

  const auto probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Render);
  auto render_sample_rate = 48000U;
  if (!probe_result.ok()) {
    std::cout << "Windows WASAPI render loop preflight skipped: probe failed\n";
  } else {
    const auto& probe = probe_result.probe();
    render_sample_rate = probe.mix_format.sample_rate;
    {
      sar::graph::Graph mismatched_graph(31,
                                         probe.mix_format.channels,
                                         probe.buffer_frames,
                                         mismatched_sample_rate(probe.mix_format.sample_rate));
      sar::diagnostics::EngineDiagnostics diagnostics;
      auto result = sar::platform::open_default_wasapi_render_loop(mismatched_graph,
                                                                   diagnostics);
      if (const auto failure =
              expect(!result.ok(), "Expected render loop sample-rate preflight failure")) {
        return failure;
      }
      if (const auto failure =
              expect(has_error_code(result, "graph_sample_rate_mismatch"),
                     "Expected render loop graph_sample_rate_mismatch")) {
        return failure;
      }
    }

    if (probe.buffer_frames > 1) {
      sar::graph::Graph undersized_graph(32,
                                         probe.mix_format.channels,
                                         probe.buffer_frames - 1,
                                         probe.mix_format.sample_rate);
      undersized_graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
      undersized_graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
      sar::diagnostics::EngineDiagnostics diagnostics;
      auto result = sar::platform::open_default_wasapi_render_loop(undersized_graph,
                                                                   diagnostics);
      if (const auto failure =
              expect(!result.ok(), "Expected render loop graph-shape preflight failure")) {
        return failure;
      }
      if (const auto failure = expect(has_error_code(result, "graph_buffer_too_small"),
                                      "Expected render loop graph_buffer_too_small")) {
        return failure;
      }
    }
  }

  sar::graph::Graph graph(21, 2, 128, render_sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto result = sar::platform::open_default_wasapi_render_loop(graph, diagnostics);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = result.take_loop();
  const auto render_diagnostics = loop->diagnostics();
  if (const auto failure = expect(render_diagnostics.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected open render stream diagnostics")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.direction ==
                                      sar::platform::WasapiStreamDirection::Render,
                                  "Expected render stream diagnostics direction")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.buffer_frames ==
                                      loop->probe().buffer_frames,
                                  "Expected render diagnostics buffer size")) {
    return failure;
  }
  const auto initial_summary = loop->summary();
  if (const auto failure = expect(!initial_summary.running,
                                  "Expected initial render summary stopped")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.error_count == 0,
                                  "Expected initial render summary without errors")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.render_stream.buffer_frames ==
                                      loop->probe().buffer_frames,
                                  "Expected render summary buffer size")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.worker.loop_cycles == 0,
                                  "Expected initial render summary without loop cycles")) {
    return failure;
  }

  auto& input = loop->input_buffer();
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = 0.0F;
    }
  }

  const auto start_result = loop->start(10);
  if (!start_result.ok()) {
    for (const auto& error : start_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  loop->stop();

  if (const auto failure = expect(!loop->running(), "Expected stopped render loop")) {
    return failure;
  }
  if (const auto failure = expect(loop->last_errors().empty(),
                                  "Expected no render loop worker errors")) {
    return failure;
  }
  const auto stats = loop->stats();
  const auto final_summary = loop->summary();
  if (const auto failure = expect(!final_summary.running,
                                  "Expected stopped render summary")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.error_count == 0,
                                  "Expected final render summary without errors")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.worker.rendered_frames ==
                                      stats.rendered_frames,
                                  "Expected render summary worker stats")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.render_stream.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected render summary open stream after stop")) {
    return failure;
  }
  if (const auto failure = expect(stats.last_captured_frames == 0,
                                  "Expected render loop without last captured frames")) {
    return failure;
  }
  if (const auto failure = expect(stats.last_rendered_frames <= loop->probe().buffer_frames,
                                  "Expected bounded last rendered frames")) {
    return failure;
  }
  if (const auto failure = expect(stats.rendered_frames >= stats.last_rendered_frames,
                                  "Expected total rendered frames to cover last render")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_idle,
                                  "Expected render loop without last capture idle")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_wait_timed_out,
                                  "Expected render loop without last capture timeout")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_partial,
                                  "Expected render loop without last capture partial")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_silent,
                                  "Expected render loop without last silent capture")) {
    return failure;
  }
  if (const auto failure = expect(stats.last_rendered_frames == 0 ||
                                      stats.last_graph_processed,
                                  "Expected non-idle render loop cycle to process graph")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_render_wait_timed_out ||
                                      stats.render_wait_timeout_cycles > 0,
                                  "Expected last render timeout to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_render_partial ||
                                      stats.render_partial_cycles > 0,
                                  "Expected last render partial to be counted")) {
    return failure;
  }

  std::cout << "Windows WASAPI render loop smoke test passed\n";
  return 0;
}
