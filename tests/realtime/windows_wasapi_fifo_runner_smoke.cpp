#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace {

sar::platform::WasapiStreamProbe make_probe(
    sar::platform::WasapiStreamDirection direction,
    std::uint32_t buffer_frames) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.mode = sar::platform::WasapiStreamMode::Endpoint;
  probe.buffer_frames = buffer_frames;
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 1;
  probe.mix_format.frames_per_block = buffer_frames;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.valid_bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return probe;
}

}  // namespace

int main() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture, 2));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 2, .samples = {{1.0F, 2.0F}}});
  capture.enqueue_capture({.frames = 2, .samples = {{3.0F, 4.0F}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render.enqueue_render({.writable_frames = 1});
  render.enqueue_render({.writable_frames = 1});
  render.enqueue_render({.writable_frames = 4});

  sar::platform::WindowsWasapiGraphRunner runner(
      &capture, &render, 1, 1, 4, 2, 4, 8);
  sar::graph::Graph graph(1, 1, 4, 48000);
  graph.add_node(std::make_unique<sar::graph::PassthroughNode>());
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto first = runner.process_once(graph, diagnostics, 1);
  assert(first.ok() && !first.stats().graph_processed);
  assert(diagnostics.capture_fifo_fill_frames == 2);
  const auto second = runner.process_once(graph, diagnostics, 1);
  assert(second.ok() && second.stats().graph_processed);
  assert(second.stats().rendered_frames == 1);
  assert(diagnostics.capture_fifo_fill_frames == 0);
  assert(diagnostics.render_fifo_fill_frames == 3);
  const auto third = runner.process_once(graph, diagnostics, 1);
  assert(third.ok() && !third.stats().graph_processed);
  assert(third.stats().rendered_frames == 1);
  assert(diagnostics.render_fifo_fill_frames == 2);
  const auto fourth = runner.process_once(graph, diagnostics, 1);
  assert(fourth.ok() && !fourth.stats().graph_processed);
  assert(fourth.stats().rendered_frames == 2);
  assert(diagnostics.render_fifo_fill_frames == 0);

  std::vector<float> rendered;
  for (const auto& submission : render.render_submissions()) {
    rendered.insert(rendered.end(), submission.samples[0].begin(),
                    submission.samples[0].end());
  }
  assert((rendered == std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
  assert(diagnostics.processed_blocks == 1);
  assert(diagnostics.capture_fifo_overflow_frames == 0);
  assert(diagnostics.render_fifo_overflow_frames == 0);
  std::cout << "Windows WASAPI FIFO runner smoke test passed\n";
  return 0;
}
