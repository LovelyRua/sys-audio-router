#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

sar::platform::WasapiStreamProbe make_probe(
    sar::platform::WasapiStreamDirection direction) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.mode = sar::platform::WasapiStreamMode::Endpoint;
  probe.buffer_frames = 64;
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 1;
  probe.mix_format.frames_per_block = 64;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.valid_bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return probe;
}

std::vector<float> samples(std::uint32_t first_frame) {
  std::vector<float> result(64);
  for (std::uint32_t frame = 0; frame < result.size(); ++frame) {
    result[frame] = static_cast<float>(first_frame + frame) / 256.0F;
  }
  return result;
}

}  // namespace

int main() {
  {
    sar::tests::ScriptedWasapiStream small_capture(
        make_probe(sar::platform::WasapiStreamDirection::Capture));
    sar::tests::ScriptedWasapiStream small_render(
        make_probe(sar::platform::WasapiStreamDirection::Render));
    bool rejected = false;
    try {
      sar::platform::WindowsWasapiGraphRunner invalid(
          &small_capture, &small_render, 1, 1, 64, 64, 64, 64, true, true);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    assert(rejected);
  }

  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render));
  for (std::uint32_t packet = 0; packet < 4; ++packet) {
    capture.enqueue_capture({.frames = 64, .samples = {samples(packet * 64)}});
  }
  render.enqueue_render({.writable_frames = 64});

  sar::platform::WindowsWasapiGraphRunner runner(
      &capture, &render, 1, 1, 64, 64, 64, 256, true, true);
  runner.set_capture_clock_feed_forward_ppm(
      std::numeric_limits<double>::infinity());
  assert(runner.capture_clock_feed_forward_ppm() == 0.0);
  runner.set_capture_clock_feed_forward_ppm(3000.0);
  assert(runner.capture_clock_feed_forward_ppm() == 2500.0);
  runner.set_capture_clock_feed_forward_ppm(-3000.0);
  assert(runner.capture_clock_feed_forward_ppm() == -2500.0);
  runner.set_capture_clock_feed_forward_ppm(500.0);
  assert(runner.capture_clock_feed_forward_ppm() == 500.0);
  sar::graph::Graph graph(1, 1, 64, 48000);
  graph.add_node(std::make_unique<sar::graph::PassthroughNode>());
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto result = runner.process_once(graph, diagnostics, 1);
  assert(result.ok());
  assert(result.stats().capture_rate_adapter_active);
  assert(!result.stats().capture_rate_adapter_reset);
  assert(result.stats().captured_frames == 256);
  assert(result.stats().graph_processed);
  assert(result.stats().capture_resampler_input_frames > 0);
  assert(result.stats().capture_resampler_input_frames <= 256);
  // The queued render deadline is submitted first, then the bounded refill may
  // produce two graph blocks to restore the configured FIFO target.
  assert(result.stats().capture_resampler_output_frames == 128);
  assert(result.stats().capture_rate_correction_ppm > 0.0);
  assert(result.stats().capture_clock_feed_forward_ppm == 500.0);
  assert(result.stats().capture_fifo_correction_ppm > 0.0);
  assert(std::abs(result.stats().capture_rate_correction_ppm -
                  (result.stats().capture_clock_feed_forward_ppm +
                   result.stats().capture_fifo_correction_ppm)) < 1.0e-9);
  assert(result.stats().capture_resampler_ratio < 1.0);
  assert(std::isfinite(result.stats().capture_resampler_ratio));
  assert(result.stats().rendered_frames == 64);
  assert(render.render_submissions().size() == 1);
  assert(render.render_submissions().front().frames == 64);
  assert(diagnostics.capture_fifo_overflow_frames == 0);

  sar::tests::ScriptedWasapiStream clamped_capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture));
  sar::tests::ScriptedWasapiStream clamped_render(
      make_probe(sar::platform::WasapiStreamDirection::Render));
  for (std::uint32_t packet = 0; packet < 4; ++packet) {
    clamped_capture.enqueue_capture(
        {.frames = 64, .samples = {samples(packet * 64)}});
  }
  clamped_render.enqueue_render({.writable_frames = 64});

  sar::platform::WindowsWasapiGraphRunner clamped_runner(
      &clamped_capture, &clamped_render, 1, 1, 64, 64, 64, 256, true, true);
  clamped_runner.set_capture_clock_feed_forward_ppm(2500.0);
  sar::diagnostics::EngineDiagnostics clamped_diagnostics;
  const auto clamped_result =
      clamped_runner.process_once(graph, clamped_diagnostics, 1);
  assert(clamped_result.ok());
  assert(clamped_result.stats().capture_rate_correction_clamped);
  assert(clamped_result.stats().capture_rate_correction_ppm == 2500.0);
  assert(clamped_result.stats().rendered_frames == 64);
}
