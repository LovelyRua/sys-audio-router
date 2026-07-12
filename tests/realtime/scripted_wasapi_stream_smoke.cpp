#include "tests/realtime/scripted_wasapi_stream.h"

#include "tests/realtime/test_helpers.h"

#include <iostream>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiStreamProbe make_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.device_id = "scripted-device";
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.buffer_frames = 4;
  return probe;
}

}  // namespace

int main() {
  sar::tests::ScriptedWasapiStream stream(make_probe());
  stream.enqueue_capture({
      3,
      {{0.1F, 0.2F, 0.3F}, {1.1F, 1.2F, 1.3F}},
      sar::platform::WasapiStreamIoStatus::Completed,
      false,
      true,
      true,
  });
  stream.enqueue_capture({
      2,
      {},
      sar::platform::WasapiStreamIoStatus::Completed,
      true,
  });
  stream.enqueue_capture({
      0,
      {},
      sar::platform::WasapiStreamIoStatus::TimedOut,
  });
  stream.enqueue_render({2, sar::platform::WasapiStreamIoStatus::Completed});
  stream.enqueue_render({0, sar::platform::WasapiStreamIoStatus::Cancelled});

  if (const auto failure = expect(stream.start().ok(), "Expected scripted start success")) {
    return failure;
  }
  stream.request_stop();
  stream.request_stop();
  if (const auto failure = expect(stream.stop().ok(), "Expected scripted stop success")) {
    return failure;
  }
  if (const auto failure = expect(stream.start_calls() == 1 && stream.stop_calls() == 1 &&
                                      stream.request_stop_calls() == 2,
                                  "Expected lifecycle calls to be recorded")) {
    return failure;
  }

  sar::realtime::AudioBuffer capture(2, 4);
  const auto captured = stream.capture_once(capture, 10);
  if (const auto failure = expect(captured.frames() == 3 && captured.data_discontinuity() &&
                                      captured.timestamp_error() && !captured.silent(),
                                  "Expected scripted capture metadata")) {
    return failure;
  }
  if (const auto failure = expect(
          sar::tests::nearly_equal(capture.channel(0)[2], 0.3F) &&
              sar::tests::nearly_equal(capture.channel(1)[1], 1.2F),
          "Expected scripted capture samples")) {
    return failure;
  }
  const auto silent = stream.capture_once(capture, 10);
  if (const auto failure = expect(silent.frames() == 2 && silent.silent(),
                                  "Expected scripted silent capture")) {
    return failure;
  }
  const auto timed_out = stream.capture_once(capture, 10);
  if (const auto failure = expect(timed_out.timed_out(), "Expected scripted timeout")) {
    return failure;
  }

  sar::realtime::AudioBuffer render(2, 4);
  render.channel(0)[0] = 2.0F;
  render.channel(0)[1] = 3.0F;
  render.channel(1)[0] = 4.0F;
  render.channel(1)[1] = 5.0F;
  const auto rendered = stream.render_once(render, 10);
  if (const auto failure = expect(rendered.frames() == 2,
                                  "Expected scripted writable render frames")) {
    return failure;
  }
  const auto& submissions = stream.render_submissions();
  if (const auto failure = expect(
          submissions.size() == 1 && submissions[0].frames == 2 &&
              sar::tests::nearly_equal(submissions[0].samples[0][1], 3.0F) &&
              sar::tests::nearly_equal(submissions[0].samples[1][0], 4.0F),
          "Expected actual rendered samples to be recorded")) {
    return failure;
  }
  const auto cancelled = stream.render_once(render, 10);
  if (const auto failure = expect(cancelled.cancelled() && submissions.size() == 1,
                                  "Expected cancelled render without submission")) {
    return failure;
  }
  if (const auto failure = expect(stream.remaining_capture_steps() == 0 &&
                                      stream.remaining_render_steps() == 0,
                                  "Expected scripts to be consumed deterministically")) {
    return failure;
  }

  const auto exhausted = stream.capture_once(capture, 10);
  if (const auto failure = expect(!exhausted.ok() &&
                                      exhausted.errors()[0].code == "capture_script_exhausted",
                                  "Expected deterministic exhaustion failure")) {
    return failure;
  }

  std::cout << "Scripted WASAPI stream smoke test passed\n";
  return 0;
}
