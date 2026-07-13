#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
sar::platform::WasapiStreamProbe probe(sar::platform::WasapiStreamDirection direction,
                                       std::uint32_t frames) {
  sar::platform::WasapiStreamProbe value;
  value.direction = direction;
  value.buffer_frames = frames;
  value.mix_format.sample_rate = 48000;
  value.mix_format.channels = 1;
  value.mix_format.frames_per_block = frames;
  value.mix_format.bits_per_sample = 32;
  value.mix_format.valid_bits_per_sample = 32;
  value.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return value;
}

sar::graph::Graph graph() {
  sar::graph::Graph value(1, 1, 4, 48000);
  value.add_node(std::make_unique<sar::graph::PassthroughNode>());
  return value;
}

std::vector<float> rendered(const sar::tests::ScriptedWasapiStream& stream) {
  std::vector<float> samples;
  for (const auto& submission : stream.render_submissions()) {
    samples.insert(samples.end(), submission.samples[0].begin(),
                   submission.samples[0].end());
  }
  return samples;
}

void idle_drains_backlog() {
  sar::tests::ScriptedWasapiStream capture(
      probe(sar::platform::WasapiStreamDirection::Capture, 4));
  sar::tests::ScriptedWasapiStream render(
      probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 4, .samples = {{1, 2, 3, 4}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render.enqueue_render({.writable_frames = 1});
  render.enqueue_render({.writable_frames = 3});
  sar::platform::WindowsWasapiGraphRunner runner(&capture, &render, 1, 1, 4, 4, 4, 8);
  auto route = graph();
  sar::diagnostics::EngineDiagnostics diagnostics;
  const auto first = runner.process_once(route, diagnostics, 1);
  assert(first.ok() && first.stats().graph_processed);
  assert(first.stats().rendered_frames == 1);
  assert(diagnostics.render_fifo_fill_frames == 3);
  const auto idle = runner.process_once(route, diagnostics, 1);
  assert(idle.ok() && idle.stats().capture_stream_idle);
  assert(!idle.stats().graph_processed && idle.stats().rendered_frames == 3);
  assert(diagnostics.render_fifo_fill_frames == 0);
  assert((rendered(render) == std::vector<float>{1, 2, 3, 4}));
}

void cancellation_preserves_queues() {
  sar::tests::ScriptedWasapiStream capture(
      probe(sar::platform::WasapiStreamDirection::Capture, 2));
  sar::tests::ScriptedWasapiStream render(
      probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.frames = 2, .samples = {{1, 2}}});
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
  capture.enqueue_capture({.frames = 2, .samples = {{3, 4}}});
  render.enqueue_render({.writable_frames = 4});
  sar::platform::WindowsWasapiGraphRunner runner(&capture, &render, 1, 1, 4, 2, 4, 8);
  auto route = graph();
  sar::diagnostics::EngineDiagnostics diagnostics;
  assert(runner.process_once(route, diagnostics, 1).ok());
  assert(diagnostics.capture_fifo_fill_frames == 2);
  const auto cancelled = runner.process_once(route, diagnostics, 1);
  assert(cancelled.ok() && cancelled.stats().cancelled);
  assert(!cancelled.stats().graph_processed);
  assert(runner.process_once(route, diagnostics, 1).stats().graph_processed);
  assert(diagnostics.capture_fifo_fill_frames == 0);
  assert((rendered(render) == std::vector<float>{1, 2, 3, 4}));

  sar::tests::ScriptedWasapiStream capture2(
      probe(sar::platform::WasapiStreamDirection::Capture, 4));
  sar::tests::ScriptedWasapiStream render2(
      probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture2.enqueue_capture({.frames = 4, .samples = {{5, 6, 7, 8}}});
  capture2.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render2.enqueue_render({.status = sar::platform::WasapiStreamIoStatus::Cancelled});
  render2.enqueue_render({.writable_frames = 4});
  sar::platform::WindowsWasapiGraphRunner runner2(&capture2, &render2, 1, 1, 4, 4, 4, 8);
  auto route2 = graph();
  sar::diagnostics::EngineDiagnostics diagnostics2;
  const auto render_cancelled = runner2.process_once(route2, diagnostics2, 1);
  assert(render_cancelled.stats().cancelled && render_cancelled.stats().graph_processed);
  assert(render2.render_submissions().empty());
  assert(diagnostics2.render_fifo_fill_frames == 4);
  const auto resumed = runner2.process_once(route2, diagnostics2, 1);
  assert(resumed.ok() && !resumed.stats().graph_processed);
  assert(resumed.stats().rendered_frames == 4);
  assert(diagnostics2.render_fifo_fill_frames == 0);
  assert((rendered(render2) == std::vector<float>{5, 6, 7, 8}));
}

void duplex_prime_supplies_render_before_capture() {
  sar::tests::ScriptedWasapiStream capture(
      probe(sar::platform::WasapiStreamDirection::Capture, 4));
  sar::tests::ScriptedWasapiStream render(
      probe(sar::platform::WasapiStreamDirection::Render, 4));
  capture.enqueue_capture({.status = sar::platform::WasapiStreamIoStatus::TimedOut});
  render.enqueue_render({.writable_frames = 4});
  sar::platform::WindowsWasapiGraphRunner runner(
      &capture, &render, 1, 1, 4, 4, 4, 8, true);
  auto route = graph();
  sar::diagnostics::EngineDiagnostics diagnostics;
  const auto primed = runner.process_once(route, diagnostics, 1);
  assert(primed.ok() && primed.stats().capture_stream_idle);
  assert(!primed.stats().graph_processed && primed.stats().rendered_frames == 4);
  assert(diagnostics.render_fifo_underflow_cycles == 0);
  assert((rendered(render) == std::vector<float>{0, 0, 0, 0}));
}
}  // namespace

int main() {
  idle_drains_backlog();
  cancellation_preserves_queues();
  duplex_prime_supplies_render_before_capture();
  return 0;
}
