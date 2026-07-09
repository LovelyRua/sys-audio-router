#include "core/platform/windows_wasapi_loop_preflight.h"

#include "core/graph/node.h"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiStreamProbe make_probe(
    sar::platform::WasapiStreamDirection direction,
    std::uint32_t sample_rate,
    std::uint16_t channels,
    std::uint32_t buffer_frames) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.device_id = direction == sar::platform::WasapiStreamDirection::Capture
                        ? "capture-device"
                        : "render-device";
  probe.device_label = direction == sar::platform::WasapiStreamDirection::Capture
                           ? "Capture Device"
                           : "Render Device";
  probe.mix_format.sample_rate = sample_rate;
  probe.mix_format.channels = channels;
  probe.mix_format.frames_per_block = buffer_frames;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = buffer_frames;
  return probe;
}

bool has_error_code(
    const std::vector<sar::platform::WasapiRealtimeWorkerError>& errors,
    const char* code) {
  for (const auto& error : errors) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

bool has_error_message(
    const std::vector<sar::platform::WasapiRealtimeWorkerError>& errors,
    const std::string& text) {
  for (const auto& error : errors) {
    if (error.message.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

sar::graph::Graph make_processing_graph(std::uint64_t id,
                                        std::size_t channels,
                                        std::size_t frames,
                                        std::uint32_t sample_rate) {
  sar::graph::Graph graph(id, channels, frames, sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
  graph.add_node(std::make_unique<sar::graph::GainNode>(1.0F));
  return graph;
}

}  // namespace

int main() {
  const auto capture_probe =
      make_probe(sar::platform::WasapiStreamDirection::Capture, 48000, 4, 96);
  const auto render_probe =
      make_probe(sar::platform::WasapiStreamDirection::Render, 48000, 2, 128);
  const auto mismatched_render_probe =
      make_probe(sar::platform::WasapiStreamDirection::Render, 44100, 2, 128);

  if (const auto failure =
          expect(sar::platform::compatible_wasapi_duplex_sample_rates(
                     capture_probe, render_probe),
                 "Expected matching duplex sample rates")) {
    return failure;
  }
  if (const auto failure =
          expect(!sar::platform::compatible_wasapi_duplex_sample_rates(
                     capture_probe, mismatched_render_probe),
                 "Expected mismatched duplex sample rates")) {
    return failure;
  }

  {
    sar::graph::Graph passthrough_graph(1, 1, 1, 48000);
    auto errors = sar::platform::validate_wasapi_render_graph_preflight(
        passthrough_graph,
        render_probe);
    if (const auto failure =
            expect(errors.empty(), "Expected passthrough render preflight success")) {
      return failure;
    }

    errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        passthrough_graph,
        capture_probe,
        render_probe);
    if (const auto failure =
            expect(errors.empty(), "Expected passthrough duplex preflight success")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(2, 2, 128, 48000);
    const auto errors =
        sar::platform::validate_wasapi_render_graph_preflight(graph, render_probe);
    if (const auto failure =
            expect(errors.empty(), "Expected shaped render preflight success")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(3, 4, 128, 48000);
    const auto errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        graph,
        capture_probe,
        render_probe);
    if (const auto failure =
            expect(errors.empty(), "Expected shaped duplex preflight success")) {
      return failure;
    }
  }

  {
    sar::graph::Graph graph(4, 2, 128, 44100);
    const auto errors =
        sar::platform::validate_wasapi_render_graph_preflight(graph, render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_sample_rate_mismatch"),
                   "Expected render sample-rate mismatch")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_message(errors, "render stream sample rate"),
                   "Expected render sample-rate error message")) {
      return failure;
    }
  }

  {
    sar::graph::Graph graph(5, 4, 128, 44100);
    const auto errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        graph,
        capture_probe,
        render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_sample_rate_mismatch"),
                   "Expected duplex capture sample-rate mismatch")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_message(errors, "capture stream sample rate"),
                   "Expected duplex capture sample-rate message")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(6, 4, 128, 48000);
    const auto errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        graph,
        capture_probe,
        mismatched_render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_sample_rate_mismatch"),
                   "Expected duplex render sample-rate mismatch")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_message(errors, "render stream sample rate"),
                   "Expected duplex render sample-rate message")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(7, 1, 128, 48000);
    const auto errors =
        sar::platform::validate_wasapi_render_graph_preflight(graph, render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_buffer_too_small"),
                   "Expected render channel preflight failure")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_message(errors, "render stream shape"),
                   "Expected render shape error message")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(8, 2, 127, 48000);
    const auto errors =
        sar::platform::validate_wasapi_render_graph_preflight(graph, render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_buffer_too_small"),
                   "Expected render frame preflight failure")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(9, 3, 128, 48000);
    const auto errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        graph,
        capture_probe,
        render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_buffer_too_small"),
                   "Expected duplex channel preflight failure")) {
      return failure;
    }
    if (const auto failure =
            expect(has_error_message(errors, "duplex stream shapes"),
                   "Expected duplex shape error message")) {
      return failure;
    }
  }

  {
    auto graph = make_processing_graph(10, 4, 127, 48000);
    const auto errors = sar::platform::validate_wasapi_duplex_graph_preflight(
        graph,
        capture_probe,
        render_probe);
    if (const auto failure =
            expect(has_error_code(errors, "graph_buffer_too_small"),
                   "Expected duplex frame preflight failure")) {
      return failure;
    }
  }

  std::cout << "Windows WASAPI loop preflight smoke test passed\n";
  return 0;
}
