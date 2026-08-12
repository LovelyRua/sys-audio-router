#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/virtual_asio_render_bus.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"
#include "tests/realtime/test_helpers.h"

#include <cassert>
#include <limits>
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

sar::platform::WasapiStreamProbe capture_probe() {
  auto probe = render_probe();
  probe.device_id = "capture";
  probe.device_label = "Capture";
  probe.direction = sar::platform::WasapiStreamDirection::Capture;
  probe.buffer_frames = 4;
  return probe;
}

class SingleBlockSource final : public sar::platform::RealtimeAudioSource {
 public:
  explicit SingleBlockSource(sar::realtime::AudioBuffer block)
      : block_(std::move(block)) {}

  [[nodiscard]] bool read(
      sar::realtime::AudioBuffer& destination) noexcept override {
    if (consumed_) {
      destination.clear();
      return false;
    }
    destination.copy_from(block_);
    consumed_ = true;
    return true;
  }

 private:
  sar::realtime::AudioBuffer block_;
  bool consumed_ = false;
};

}  // namespace

int main() {
  {
    sar::tests::ScriptedWasapiStream capture(capture_probe());
    capture.enqueue_capture({
        .frames = 4,
        .samples = {{0.75F,
                     -0.75F,
                     std::numeric_limits<float>::quiet_NaN(),
                     0.5F},
                    {0.1F, 0.1F, 0.1F, 0.1F}},
    });
    assert(capture.start().ok());

    sar::realtime::AudioBuffer external_block(2, 4);
    external_block.channel(0)[0] = 0.5F;
    external_block.channel(0)[1] = -0.5F;
    external_block.channel(0)[2] = 0.25F;
    external_block.channel(0)[3] = std::numeric_limits<float>::infinity();
    for (std::size_t frame = 0; frame < external_block.frames(); ++frame) {
      external_block.channel(1)[frame] = 0.2F;
    }
    SingleBlockSource external(std::move(external_block));

    sar::platform::WindowsWasapiGraphRunner duplex_runner(
        &capture, nullptr, 2, 2, 4, 4, 0, 12, false, false, &external);
    sar::graph::Graph duplex_graph(2, 2, 4, 48000);
    sar::diagnostics::EngineDiagnostics duplex_diagnostics;
    const auto duplex_result =
        duplex_runner.process_once(duplex_graph, duplex_diagnostics, 0);

    assert(duplex_result.ok());
    assert(duplex_result.stats().graph_processed);
    assert(duplex_result.stats().external_input_mixed);
    assert(duplex_result.stats().external_input_clipped_samples == 2);
    assert(duplex_result.stats().external_input_non_finite_samples == 2);
    const auto& mixed = duplex_runner.output_buffer();
    assert(sar::tests::nearly_equal(mixed.channel(0)[0], 1.0F));
    assert(sar::tests::nearly_equal(mixed.channel(0)[1], -1.0F));
    assert(sar::tests::nearly_equal(mixed.channel(0)[2], 0.25F));
    assert(sar::tests::nearly_equal(mixed.channel(0)[3], 0.5F));
    for (std::size_t frame = 0; frame < mixed.frames(); ++frame) {
      assert(sar::tests::nearly_equal(mixed.channel(1)[frame], 0.3F));
    }
  }

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

  render.enqueue_render({.writable_frames = 10});
  const auto empty_result = runner.process_once(graph, diagnostics, 10);
  assert(empty_result.ok());
  assert(!empty_result.stats().graph_processed);
  assert(empty_result.stats().rendered_frames == 10);
  assert(!empty_result.stats().render_stream_idle);
  assert(diagnostics.processed_blocks == 4);
  assert(diagnostics.render_fifo_underflow_cycles == 0);
  assert(diagnostics.render_fifo_underflow_frames == 0);
  assert(render.render_submissions().size() == 4);
  const auto& silence = render.render_submissions().back();
  for (const auto& channel : silence.samples) {
    for (const auto sample : channel) {
      assert(sample == 0.0F);
    }
  }

  std::cout << "WASAPI external input smoke test passed\n";
  return 0;
}
