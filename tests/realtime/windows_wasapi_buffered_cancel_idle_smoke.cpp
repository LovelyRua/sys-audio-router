#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <cassert>
#include <cstdint>
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

sar::graph::Graph make_graph() {
  sar::graph::Graph graph(1, 1, 4, 48000);
  graph.add_node(std::make_unique<sar::graph::PassthroughNode>());
  return graph;
}

std::vector<float> rendered_samples(
    const sar::tests::ScriptedWasapiStream& render) {
  std::vector<float> samples;
  for (const auto& submission : render.render_submissions()) {
    samples.insert(samples.end(), submission.samples[0].begin(),
                   submission.samples[0].end());
  }
  return samples;
}

void capture_idle_drains_render_backlog() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture, 4));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 4, .samples = {{1.0F, 2.0F, 3.0F, 4.0F}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render.enqueue_render({.writable_frames = 1});
  render.enqueue_render({.writable_frames = 3});

  sar::platform::WindowsWasapiGraphRunner runner(&capture, &render, 1, 1, 4, 4, 4, 8);
  auto graph = make_graph();
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto first = runner.process_once(graph, diagnostics, 1);
  assert(first.ok() && first.stats().graph_processed);
  assert(first.stats().rendered_frames == 1);
  assert(diagnostics.render_fifo_fill_frames == 3);

  const auto idle = runner.process_once(graph, diagnostics, 1);
  assert(idle.ok() && idle.stats().capture_stream_idle);
  assert(!idle.stats().graph_processed && idle.stats().rendered_frames == 3);
  assert(diagnostics.render_fifo_fill_frames == 0);
  assert((rendered_samples(render) == std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
}

void capture_cancellation_preserves_fifo() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture, 2));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 2, .samples = {{1.0F, 2.0F}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
  capture.enqueue_capture({.frames = 2, .samples = {{3.0F, 4.0F}}});
  render.enqueue_render({.writable_frames = 4});

  sar::platform::WindowsWasapiGraphRunner runner(&capture, &render, 1, 1, 4, 2, 4, 8);
  auto graph = make_graph();
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto first = runner.process_once(graph, diagnostics, 1);
  assert(first.ok() && diagnostics.capture_fifo_fill_frames == 2);
  const auto cancelled = runner.process_once(graph, diagnostics, 1);
  assert(cancelled.ok() && cancelled.stats().cancelled);
  assert(!cancelled.stats().graph_processed);

  const auto resumed = runner.process_once(graph, diagnostics, 1);
  assert(resumed.ok() && resumed.stats().graph_processed);
  assert(diagnostics.capture_fifo_fill_frames == 0);
  assert((rendered_samples(render) == std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
}

void render_cancellation_preserves_staged_frames() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture, 4));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 4, .samples = {{1.0F, 2.0F, 3.0F, 4.0F}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render.enqueue_render({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
  render.enqueue_render({.writable_frames = 4});

  sar::platform::WindowsWasapiGraphRunner runner(&capture, &render, 1, 1, 4, 4, 4, 8);
  auto graph = make_graph();
  sar::diagnostics::EngineDiagnostics diagnostics;

  const auto cancelled = runner.process_once(graph, diagnostics, 1);
  assert(cancelled.ok() && cancelled.stats().cancelled);
  assert(cancelled.stats().graph_processed);
  assert(render.render_submissions().empty());

  const auto resumed = runner.process_once(graph, diagnostics, 1);
  assert(resumed.ok() && !resumed.stats().graph_processed);
  assert(resumed.stats().rendered_frames == 4);
  assert(diagnostics.render_fifo_fill_frames == 0);
  assert((rendered_samples(render) == std::vector<float>{1.0F, 2.0F, 3.0F, 4.0F}));
}

}  // namespace

int main() {
  capture_idle_drains_render_backlog();
  capture_cancellation_preserves_fifo();
  render_cancellation_preserves_staged_frames();
  return 0;
}
