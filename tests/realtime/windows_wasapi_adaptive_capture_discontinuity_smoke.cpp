#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_graph_runner.h"
#include "tests/realtime/scripted_wasapi_stream.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

constexpr std::size_t kGraphFrames = 64;
constexpr std::uint32_t kCaptureFrames = 32;
constexpr std::size_t kFifoFrames = 128;
constexpr std::size_t kLearningCycles = 3;
constexpr float kPreGapSample = -0.75F;
constexpr float kPostGapSample = 0.5F;

sar::platform::WasapiStreamProbe make_probe(
    sar::platform::WasapiStreamDirection direction) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.mode = sar::platform::WasapiStreamMode::Endpoint;
  probe.buffer_frames = kCaptureFrames;
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 1;
  probe.mix_format.frames_per_block = kCaptureFrames;
  probe.mix_format.bits_per_sample = 32;
  probe.mix_format.valid_bits_per_sample = 32;
  probe.mix_format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return probe;
}

std::vector<float> constant_packet(float sample) {
  return std::vector<float>(kCaptureFrames, sample);
}

class RecordingPassthroughNode final : public sar::graph::Node {
 public:
  RecordingPassthroughNode() : observed_(kGraphFrames, 0.0F) {}

  void process(const sar::realtime::ProcessContext&,
               const sar::realtime::AudioBuffer& input,
               sar::realtime::AudioBuffer& output) noexcept override {
    ++process_calls_;
    const auto samples = input.channel(0);
    std::copy(samples.begin(), samples.end(), observed_.begin());
    output.copy_from(input);
  }

  [[nodiscard]] std::size_t process_calls() const noexcept {
    return process_calls_;
  }

  [[nodiscard]] const std::vector<float>& observed() const noexcept {
    return observed_;
  }

 private:
  std::vector<float> observed_;
  std::size_t process_calls_ = 0;
};

void enqueue_capture_packet(sar::tests::ScriptedWasapiStream& capture,
                            float sample,
                            bool data_discontinuity = false) {
  capture.enqueue_capture({
      .frames = kCaptureFrames,
      .samples = {constant_packet(sample)},
      .data_discontinuity = data_discontinuity,
  });
}

void end_capture_cycle(sar::tests::ScriptedWasapiStream& capture,
                       sar::tests::ScriptedWasapiStream& render) {
  capture.enqueue_capture({
      .status = sar::platform::WasapiStreamIoStatus::Idle,
  });
  render.enqueue_render({.writable_frames = kGraphFrames});
}

sar::platform::WasapiGraphRunnerResult process_cycle(
    sar::platform::WindowsWasapiGraphRunner& runner,
    sar::graph::Graph& graph,
    sar::diagnostics::EngineDiagnostics& diagnostics) {
  return runner.process_once(graph, diagnostics, 1);
}

void assert_no_capture_overflow(
    const sar::diagnostics::EngineDiagnostics& diagnostics) {
  assert(diagnostics.capture_fifo_overflow_cycles == 0);
  assert(diagnostics.capture_fifo_overflow_frames == 0);
}

struct RunResult {
  std::vector<float> graph_input;
  std::vector<float> rendered_output;
};

RunResult run_post_gap_baseline() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render));
  sar::platform::WindowsWasapiGraphRunner runner(
      &capture, &render, 1, 1, kGraphFrames, kCaptureFrames, kGraphFrames,
      kFifoFrames, true, true);
  sar::graph::Graph graph(1, 1, kGraphFrames, 48000);
  auto recorder = std::make_unique<RecordingPassthroughNode>();
  auto* recorder_ptr = recorder.get();
  graph.add_node(std::move(recorder));
  sar::diagnostics::EngineDiagnostics diagnostics;

  for (std::size_t cycle = 0; cycle < 2; ++cycle) {
    enqueue_capture_packet(capture, kPostGapSample, cycle == 0);
    end_capture_cycle(capture, render);
    const auto result = process_cycle(runner, graph, diagnostics);
    assert(result.ok());
    assert(result.stats().capture_rate_adapter_active);
    assert(result.stats().graph_processed == (cycle == 1));
    assert_no_capture_overflow(diagnostics);
  }

  assert(recorder_ptr->process_calls() == 1);
  assert(render.render_submissions().size() == 2);
  return {recorder_ptr->observed(),
          render.render_submissions().back().samples.front()};
}

RunResult run_with_pre_gap_partial() {
  sar::tests::ScriptedWasapiStream capture(
      make_probe(sar::platform::WasapiStreamDirection::Capture));
  sar::tests::ScriptedWasapiStream render(
      make_probe(sar::platform::WasapiStreamDirection::Render));
  sar::platform::WindowsWasapiGraphRunner runner(
      &capture, &render, 1, 1, kGraphFrames, kCaptureFrames, kGraphFrames,
      kFifoFrames, true, true);
  sar::graph::Graph graph(1, 1, kGraphFrames, 48000);
  auto recorder = std::make_unique<RecordingPassthroughNode>();
  auto* recorder_ptr = recorder.get();
  graph.add_node(std::move(recorder));
  sar::diagnostics::EngineDiagnostics diagnostics;

  for (std::size_t cycle = 0; cycle < kLearningCycles; ++cycle) {
    const auto packet_count = cycle == 0 ? 3 : 2;
    for (std::size_t packet = 0; packet < packet_count; ++packet) {
      enqueue_capture_packet(capture, kPreGapSample);
    }
    end_capture_cycle(capture, render);
    const auto learning = process_cycle(runner, graph, diagnostics);
    assert(learning.ok());
    assert(learning.stats().graph_processed);
    assert(std::fabs(learning.stats().capture_fifo_correction_ppm) > 1.0e-12);
    assert_no_capture_overflow(diagnostics);
  }
  const auto learned_process_calls = recorder_ptr->process_calls();

  enqueue_capture_packet(capture, kPreGapSample);
  end_capture_cycle(capture, render);
  const auto pre_gap = process_cycle(runner, graph, diagnostics);
  assert(pre_gap.ok());
  assert(!pre_gap.stats().graph_processed);
  assert(pre_gap.stats().capture_resampler_output_frames > 0);
  assert(pre_gap.stats().capture_resampler_output_frames < kGraphFrames);
  assert(std::fabs(pre_gap.stats().capture_fifo_correction_ppm) > 1.0e-12);
  const auto learned_correction_ppm =
      pre_gap.stats().capture_fifo_correction_ppm;
  assert(recorder_ptr->process_calls() == learned_process_calls);
  assert_no_capture_overflow(diagnostics);

  enqueue_capture_packet(capture, kPostGapSample, true);
  end_capture_cycle(capture, render);
  const auto gap = process_cycle(runner, graph, diagnostics);
  assert(gap.ok());
  assert(gap.stats().capture_data_discontinuity);
  assert(gap.stats().capture_rate_adapter_reset);
  assert(gap.stats().capture_rate_adapter_recovering);
  assert(gap.stats().render_recovery_silence);
  assert(gap.stats().render_recovery_silence_frames == kGraphFrames);
  assert(!gap.stats().graph_processed);
  assert(gap.stats().capture_resampler_output_frames == 0);
  assert(diagnostics.capture_fifo_fill_frames == kCaptureFrames);
  assert(diagnostics.xrun_count == 1);
  assert(diagnostics.render_fifo_underflow_cycles == 1);
  assert(diagnostics.render_fifo_underflow_frames == kGraphFrames);
  assert(recorder_ptr->process_calls() == learned_process_calls);
  assert_no_capture_overflow(diagnostics);

  end_capture_cycle(capture, render);
  const auto post_gap_idle = process_cycle(runner, graph, diagnostics);
  assert(post_gap_idle.ok());
  assert(!post_gap_idle.stats().graph_processed);
  assert(post_gap_idle.stats().capture_rate_adapter_recovering);
  assert(post_gap_idle.stats().render_recovery_silence);
  assert(std::fabs(post_gap_idle.stats().capture_fifo_correction_ppm -
                   learned_correction_ppm) < 1.0e-9);
  assert(diagnostics.render_fifo_underflow_cycles == 2);
  assert(diagnostics.render_fifo_underflow_frames == 2 * kGraphFrames);
  assert(recorder_ptr->process_calls() == learned_process_calls);
  assert_no_capture_overflow(diagnostics);

  enqueue_capture_packet(capture, kPostGapSample);
  end_capture_cycle(capture, render);
  const auto recovered = process_cycle(runner, graph, diagnostics);
  assert(recovered.ok());
  assert(recovered.stats().graph_processed);
  assert(std::fabs(recovered.stats().capture_fifo_correction_ppm) > 1.0e-12);
  assert(!recovered.stats().capture_rate_adapter_recovering);
  assert(!recovered.stats().render_recovery_silence);
  assert(recovered.stats().render_recovery_silence_frames == 0);
  assert(diagnostics.render_fifo_underflow_cycles == 2);
  assert(diagnostics.render_fifo_underflow_frames == 2 * kGraphFrames);
  assert_no_capture_overflow(diagnostics);

  assert(recorder_ptr->process_calls() == learned_process_calls + 1);
  assert(render.render_submissions().size() == kLearningCycles + 4);
  return {recorder_ptr->observed(),
          render.render_submissions().back().samples.front()};
}

void assert_same_samples(const std::vector<float>& actual,
                         const std::vector<float>& expected) {
  assert(actual.size() == expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    assert(std::isfinite(actual[index]));
    assert(std::fabs(actual[index] - expected[index]) < 1.0e-6F);
  }
}

}  // namespace

int main() {
  const auto baseline = run_post_gap_baseline();
  const auto discontinuous = run_with_pre_gap_partial();

  assert_same_samples(discontinuous.graph_input, baseline.graph_input);
  assert_same_samples(discontinuous.rendered_output, baseline.rendered_output);
}
