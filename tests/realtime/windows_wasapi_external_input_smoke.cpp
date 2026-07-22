#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/virtual_asio_render_bus.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"
#include "tests/realtime/test_helpers.h"

#include <cassert>
#include <iostream>

namespace {

sar::platform::WasapiStreamProbe render_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.device_id = "render";
  probe.device_label = "Render";
  probe.direction = sar::platform::WasapiStreamDirection::Render;
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.mix_format.frames_per_block = 10;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.sample_format =
      sar::platform::AudioSampleFormat::IeeeFloat;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = 10;
  return probe;
}

}  // namespace

int main() {
  sar::platform::VirtualAsioRenderBus bus(2, 4, 1, 4);
  auto producer = bus.attach();
  assert(producer.valid());

  sar::realtime::AudioBuffer source(2, 4);
  for (std::size_t frame = 0; frame < source.frames(); ++frame) {
    source.channel(0)[frame] = 0.25F + static_cast<float>(frame) * 0.1F;
    source.channel(1)[frame] = -0.25F - static_cast<float>(frame) * 0.1F;
  }
  assert(producer.push(source));

  sar::tests::ScriptedWasapiStream render(render_probe());
  render.enqueue_render({.writable_frames = 4});
  sar::platform::WindowsWasapiGraphRunner runner(
      nullptr, &render, 2, 2, 4, 0, 10, 20, false, false, &bus);
  sar::graph::Graph graph(1, 2, 4, 48000);
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto result = runner.process_once(graph, diagnostics, 0);
  assert(result.ok());
  assert(result.stats().graph_processed);
  assert(result.stats().rendered_frames == 4);
  assert(render.render_submissions().size() == 1);
  const auto& rendered = render.render_submissions().front();
  for (std::size_t channel = 0; channel < 2; ++channel) {
    for (std::size_t frame = 0; frame < 4; ++frame) {
      assert(sar::tests::nearly_equal(rendered.samples[channel][frame],
                                      source.channel(channel)[frame]));
    }
  }

  assert(producer.push(source));
  assert(producer.push(source));
  assert(producer.push(source));
  render.enqueue_render({.writable_frames = 10});
  const auto batched_result = runner.process_once(graph, diagnostics, 0);
  assert(batched_result.ok());
  assert(batched_result.stats().graph_processed);
  assert(batched_result.stats().rendered_frames == 10);
  assert(diagnostics.processed_blocks == 4);
  assert(render.render_submissions().size() == 2);
  const auto& batched = render.render_submissions().back();
  for (std::size_t channel = 0; channel < 2; ++channel) {
    for (std::size_t frame = 0; frame < 10; ++frame) {
      assert(sar::tests::nearly_equal(
          batched.samples[channel][frame],
          source.channel(channel)[frame % source.frames()]));
    }
  }

  render.enqueue_render({.writable_frames = 2});
  const auto tail_result = runner.process_once(graph, diagnostics, 0);
  assert(tail_result.ok());
  assert(!tail_result.stats().graph_processed);
  assert(tail_result.stats().rendered_frames == 2);
  assert(diagnostics.processed_blocks == 4);
  assert(render.render_submissions().size() == 3);
  const auto& tail = render.render_submissions().back();
  for (std::size_t channel = 0; channel < 2; ++channel) {
    for (std::size_t frame = 0; frame < 2; ++frame) {
      assert(sar::tests::nearly_equal(
          tail.samples[channel][frame], source.channel(channel)[frame + 2]));
    }
  }

  const auto empty_result = runner.process_once(graph, diagnostics, 0);
  assert(empty_result.ok());
  assert(!empty_result.stats().graph_processed);
  assert(empty_result.stats().rendered_frames == 0);
  assert(empty_result.stats().render_stream_idle);
  assert(diagnostics.processed_blocks == 4);
  assert(diagnostics.render_fifo_underflow_cycles == 1);
  assert(diagnostics.render_fifo_underflow_frames == 4);
  assert(render.render_submissions().size() == 3);

  std::cout << "WASAPI external input smoke test passed\n";
  return 0;
}
