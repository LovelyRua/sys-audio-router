#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include "core/graph/node.h"

#include <algorithm>
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

struct DefaultEndpointAvailability {
  bool capture = false;
  bool render = false;
};

DefaultEndpointAvailability default_endpoint_availability() {
  DefaultEndpointAvailability availability;
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return availability;
  }

  for (const auto& device : result.devices()) {
    if (!device.is_default) {
      continue;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Input) {
      availability.capture = true;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Output) {
      availability.render = true;
    }
  }
  return availability;
}

bool has_error_code(const sar::platform::WasapiDuplexLoopOpenResult& result,
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
  const auto availability = default_endpoint_availability();
  if (!availability.capture || !availability.render) {
    std::cout << "Windows WASAPI duplex loop skipped: missing default endpoint\n";
    return 0;
  }

  const auto capture_probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Capture);
  const auto render_probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Render);
  auto duplex_sample_rate = 48000U;
  if (!capture_probe_result.ok() || !render_probe_result.ok()) {
    std::cout << "Windows WASAPI duplex loop preflight skipped: probe failed\n";
  } else if (capture_probe_result.probe().mix_format.sample_rate ==
             render_probe_result.probe().mix_format.sample_rate) {
    const auto& capture_probe = capture_probe_result.probe();
    const auto& render_probe = render_probe_result.probe();
    duplex_sample_rate = capture_probe.mix_format.sample_rate;
    {
      sar::graph::Graph mismatched_graph(
          33,
          std::max(capture_probe.mix_format.channels, render_probe.mix_format.channels),
          std::max(capture_probe.buffer_frames, render_probe.buffer_frames),
          mismatched_sample_rate(capture_probe.mix_format.sample_rate));
      sar::diagnostics::EngineDiagnostics diagnostics;
      auto result = sar::platform::open_default_wasapi_duplex_loop(mismatched_graph,
                                                                   diagnostics);
      if (const auto failure =
              expect(!result.ok(), "Expected duplex loop sample-rate preflight failure")) {
        return failure;
      }
      if (const auto failure =
              expect(has_error_code(result, "graph_sample_rate_mismatch"),
                     "Expected duplex loop graph_sample_rate_mismatch")) {
        return failure;
      }
    }

    const auto required_frames =
        std::max(capture_probe.buffer_frames, render_probe.buffer_frames);
    if (required_frames > 1) {
      sar::graph::Graph undersized_graph(
          34,
          std::max(capture_probe.mix_format.channels, render_probe.mix_format.channels),
          required_frames - 1,
          capture_probe.mix_format.sample_rate);
      undersized_graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
      undersized_graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
      sar::diagnostics::EngineDiagnostics diagnostics;
      auto result = sar::platform::open_default_wasapi_duplex_loop(undersized_graph,
                                                                   diagnostics);
      if (const auto failure =
              expect(!result.ok(), "Expected duplex loop graph-shape preflight failure")) {
        return failure;
      }
      if (const auto failure = expect(has_error_code(result, "graph_buffer_too_small"),
                                      "Expected duplex loop graph_buffer_too_small")) {
        return failure;
      }
    }
  }

  sar::graph::Graph graph(22, 2, 128, duplex_sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto result = sar::platform::open_default_wasapi_duplex_loop(graph, diagnostics);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      if (error.code == "duplex_sample_rate_mismatch") {
        std::cout << "Windows WASAPI duplex loop skipped: " << error.message << '\n';
        return 0;
      }
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = result.take_loop();
  const auto capture_diagnostics = loop->capture_diagnostics();
  const auto render_diagnostics = loop->render_diagnostics();
  if (const auto failure = expect(capture_diagnostics.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected open capture stream diagnostics")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected open render stream diagnostics")) {
    return failure;
  }
  if (const auto failure = expect(capture_diagnostics.direction ==
                                      sar::platform::WasapiStreamDirection::Capture,
                                  "Expected capture stream diagnostics direction")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.direction ==
                                      sar::platform::WasapiStreamDirection::Render,
                                  "Expected render stream diagnostics direction")) {
    return failure;
  }
  if (const auto failure = expect(capture_diagnostics.buffer_frames ==
                                      loop->capture_probe().buffer_frames,
                                  "Expected capture diagnostics buffer size")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.buffer_frames ==
                                      loop->render_probe().buffer_frames,
                                  "Expected render diagnostics buffer size")) {
    return failure;
  }
  const auto initial_summary = loop->summary();
  if (const auto failure = expect(!initial_summary.running,
                                  "Expected initial duplex summary stopped")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.error_count == 0,
                                  "Expected initial duplex summary without errors")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.capture_stream.buffer_frames ==
                                      loop->capture_probe().buffer_frames,
                                  "Expected duplex summary capture buffer size")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.render_stream.buffer_frames ==
                                      loop->render_probe().buffer_frames,
                                  "Expected duplex summary render buffer size")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.worker.loop_cycles == 0,
                                  "Expected initial duplex summary without loop cycles")) {
    return failure;
  }

  if (const auto failure = expect(loop->capture_probe().mix_format.sample_rate ==
                                      loop->render_probe().mix_format.sample_rate,
                                  "Expected matching duplex sample rate")) {
    return failure;
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

  if (const auto failure = expect(!loop->running(), "Expected stopped duplex loop")) {
    return failure;
  }
  if (const auto failure = expect(loop->last_errors().empty(),
                                  "Expected no duplex loop worker errors")) {
    return failure;
  }
  const auto stats = loop->stats();
  const auto final_summary = loop->summary();
  if (const auto failure = expect(!final_summary.running,
                                  "Expected stopped duplex summary")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.error_count == 0,
                                  "Expected final duplex summary without errors")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.worker.captured_frames ==
                                      stats.captured_frames,
                                  "Expected duplex summary captured frames")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.worker.rendered_frames ==
                                      stats.rendered_frames,
                                  "Expected duplex summary rendered frames")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.capture_stream.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected duplex summary open capture stream after stop")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.render_stream.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected duplex summary open render stream after stop")) {
    return failure;
  }
  if (const auto failure =
          expect(stats.last_captured_frames <= loop->capture_probe().buffer_frames,
                 "Expected bounded last captured frames")) {
    return failure;
  }
  if (const auto failure =
          expect(stats.last_rendered_frames <= loop->render_probe().buffer_frames,
                 "Expected bounded last rendered frames")) {
    return failure;
  }
  if (const auto failure = expect(stats.captured_frames >= stats.last_captured_frames,
                                  "Expected total captured frames to cover last capture")) {
    return failure;
  }
  if (const auto failure = expect(stats.rendered_frames >= stats.last_rendered_frames,
                                  "Expected total rendered frames to cover last render")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_wait_timed_out ||
                                      stats.capture_wait_timeout_cycles > 0,
                                  "Expected last capture timeout to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_render_wait_timed_out ||
                                      stats.render_wait_timeout_cycles > 0,
                                  "Expected last render timeout to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_partial ||
                                      stats.capture_partial_cycles > 0,
                                  "Expected last capture partial to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_render_partial ||
                                      stats.render_partial_cycles > 0,
                                  "Expected last render partial to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_silent ||
                                      stats.capture_silent_cycles > 0,
                                  "Expected last silent capture to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_capture_idle ||
                                      stats.capture_idle_cycles > 0,
                                  "Expected last capture idle to be counted")) {
    return failure;
  }
  if (const auto failure = expect(!stats.last_render_idle ||
                                      stats.render_idle_cycles > 0,
                                  "Expected last render idle to be counted")) {
    return failure;
  }

  std::cout << "Windows WASAPI duplex loop smoke test passed\n";
  return 0;
}
