#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include "core/graph/node.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>

namespace sar::platform {

struct WindowsWasapiDuplexLoopTestAccess {
  using ProbeStreamFunction = WasapiStreamProbeResult (*)(
      const std::string* device_id,
      WasapiStreamDirection direction,
      void* context);
  using OpenStreamFunction = WasapiStreamOpenResult (*)(
      WasapiStreamProbe probe,
      std::uint32_t requested_sample_rate,
      void* context);

  static WasapiDuplexLoopOpenResult open(
      const std::string* capture_device_id,
      const std::string* render_device_id,
      graph::Graph& graph,
      diagnostics::EngineDiagnostics& diagnostics,
      ProbeStreamFunction probe_stream,
      OpenStreamFunction open_stream,
      void* context) {
    return WindowsWasapiDuplexLoop::open(capture_device_id,
                                         render_device_id,
                                         graph,
                                         diagnostics,
                                         probe_stream,
                                         open_stream,
                                         context,
                                         nullptr);
  }
};

}  // namespace sar::platform

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

sar::platform::WasapiStreamProbe make_scripted_probe(
    sar::platform::WasapiStreamDirection direction,
    std::string device_id) {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = direction;
  probe.device_id = std::move(device_id);
  probe.device_label = direction == sar::platform::WasapiStreamDirection::Render
                           ? "Scripted render"
                           : "Scripted capture";
  probe.mix_format.sample_rate =
      direction == sar::platform::WasapiStreamDirection::Render ? 48000 : 44100;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = probe.mix_format.frames_per_block;
  return probe;
}

struct ScriptedDuplexOpen {
  int next_step = 0;
  bool order_ok = true;
  bool fail_capture_open = false;
  bool render_used_default = false;
  bool capture_used_default = false;
  std::string requested_render_id;
  std::string requested_capture_id;
  std::uint32_t render_requested_sample_rate = 1;
  std::uint32_t capture_requested_sample_rate = 1;
};

sar::platform::WasapiStreamProbeResult scripted_probe(
    const std::string* device_id,
    sar::platform::WasapiStreamDirection direction,
    void* context) {
  auto& script = *static_cast<ScriptedDuplexOpen*>(context);
  const auto expected_step =
      direction == sar::platform::WasapiStreamDirection::Render ? 0 : 2;
  script.order_ok = script.order_ok && script.next_step == expected_step;
  ++script.next_step;
  if (direction == sar::platform::WasapiStreamDirection::Render) {
    script.render_used_default = device_id == nullptr;
    script.requested_render_id = device_id == nullptr ? "" : *device_id;
    return sar::platform::WasapiStreamProbeResult::success(
        make_scripted_probe(direction, "stable-render-id"));
  }

  script.capture_used_default = device_id == nullptr;
  script.requested_capture_id = device_id == nullptr ? "" : *device_id;
  return sar::platform::WasapiStreamProbeResult::success(
      make_scripted_probe(direction, "stable-capture-id"));
}

sar::platform::WasapiStreamOpenResult scripted_open(
    sar::platform::WasapiStreamProbe probe,
    std::uint32_t requested_sample_rate,
    void* context) {
  auto& script = *static_cast<ScriptedDuplexOpen*>(context);
  const auto render =
      probe.direction == sar::platform::WasapiStreamDirection::Render;
  script.order_ok = script.order_ok && script.next_step == (render ? 1 : 3);
  ++script.next_step;
  if (render) {
    script.render_requested_sample_rate = requested_sample_rate;
  } else {
    script.capture_requested_sample_rate = requested_sample_rate;
    if (script.fail_capture_open) {
      return sar::platform::WasapiStreamOpenResult::failure({{
          "scripted_capture_open_failed",
          "Scripted capture open failure.",
          -123,
          456,
      }});
    }
  }

  sar::platform::WindowsWasapiStream stream;
  const auto result = stream.open(std::move(probe));
  if (!result.ok()) {
    return sar::platform::WasapiStreamOpenResult::failure(result.errors());
  }
  return sar::platform::WasapiStreamOpenResult::success(std::move(stream));
}

}  // namespace

int main() {
  {
    sar::platform::WasapiDuplexClockFeedForwardTracker tracker;
    const sar::platform::WasapiDuplexClockObservation warming_up;
    sar::platform::WasapiDuplexClockObservation ready{
        sar::platform::WasapiDuplexClockObservationStatus::Ready};
    ready.feed_forward.valid = true;
    ready.feed_forward.correction_ppm = 25.0;
    const sar::platform::WasapiDuplexClockObservation invalid{
        sar::platform::WasapiDuplexClockObservationStatus::Invalid};

    static_cast<void>(tracker.record(warming_up));
    if (const auto failure = expect(
            tracker.record(ready) ==
                sar::platform::WasapiDuplexClockFeedForwardAction::Apply,
            "Expected valid feed-forward observation to apply")) {
      return failure;
    }
    const auto first_invalid = tracker.record(invalid);
    const auto second_invalid = tracker.record(invalid);
    const auto third_invalid = tracker.record(invalid);
    const auto disabled_invalid = tracker.record(invalid);
    if (const auto failure = expect(
            first_invalid ==
                    sar::platform::WasapiDuplexClockFeedForwardAction::None &&
                second_invalid ==
                    sar::platform::WasapiDuplexClockFeedForwardAction::None &&
                third_invalid ==
                    sar::platform::WasapiDuplexClockFeedForwardAction::Disable &&
                disabled_invalid ==
                    sar::platform::WasapiDuplexClockFeedForwardAction::None,
            "Expected third invalid observation to disable feed-forward")) {
      return failure;
    }
    auto tracker_diagnostics = tracker.diagnostics();
    if (const auto failure = expect(
            tracker_diagnostics.ready_observations == 1 &&
                tracker_diagnostics.invalid_observations == 3 &&
                tracker_diagnostics.warming_up_observations == 1 &&
                tracker_diagnostics.disabled_observations == 1,
            "Expected partitioned feed-forward observation diagnostics")) {
      return failure;
    }

    tracker.reset();
    tracker_diagnostics = tracker.diagnostics();
    if (const auto failure = expect(
            tracker_diagnostics.ready_observations == 0 &&
                tracker_diagnostics.invalid_observations == 0 &&
                tracker_diagnostics.warming_up_observations == 0 &&
                tracker_diagnostics.disabled_observations == 0,
            "Expected feed-forward diagnostics reset at lifecycle start")) {
      return failure;
    }

    static_cast<void>(tracker.record(invalid));
    static_cast<void>(tracker.record(invalid));
    static_cast<void>(tracker.record(invalid));
    static_cast<void>(tracker.record(invalid));
    static_cast<void>(tracker.record(ready));
    static_cast<void>(tracker.record(invalid));
    tracker_diagnostics = tracker.diagnostics();
    if (const auto failure = expect(
            tracker_diagnostics.ready_observations == 1 &&
                tracker_diagnostics.invalid_observations == 4 &&
                tracker_diagnostics.disabled_observations == 1,
            "Expected ready observation to leave disabled state")) {
      return failure;
    }
  }

  const sar::platform::WasapiClockSnapshot plausible_capture_clock{
      3840, 10000000, 384000};
  if (const auto failure = expect(
          !sar::platform::wasapi_capture_clock_baseline_is_trustworthy(
              0, plausible_capture_clock),
          "Expected capture clock baseline to wait for the first packet")) {
    return failure;
  }
  if (const auto failure = expect(
          sar::platform::wasapi_capture_clock_baseline_is_trustworthy(
              480, plausible_capture_clock),
          "Expected capture clock baseline after the first packet")) {
    return failure;
  }
  if (const auto failure = expect(
          !sar::platform::wasapi_capture_clock_baseline_is_trustworthy(
              480, {3840, 10000000, 0}),
          "Expected capture clock baseline to reject zero frequency")) {
    return failure;
  }
  if (const auto failure = expect(
          !sar::platform::wasapi_capture_clock_baseline_is_trustworthy(
              480, {3840, 0, 384000}),
          "Expected capture clock baseline to reject missing QPC timestamp")) {
    return failure;
  }

  std::uint64_t converted_frames = 1;
  if (const auto failure = expect(
          sar::platform::wasapi_clock_position_to_audio_frames(
              384000, 384000, 48000, converted_frames) &&
              converted_frames == 48000,
          "Expected IAudioClock units converted to audio frames")) {
    return failure;
  }
  if (const auto failure = expect(
          sar::platform::wasapi_clock_position_to_audio_frames(
              384004, 384000, 48000, converted_frames) &&
              converted_frames == 48000,
          "Expected fractional IAudioClock units rounded down")) {
    return failure;
  }
  std::uint64_t baseline_frames = 0;
  const sar::realtime::ClockDomain clock_domain{1, 48000};
  const auto converted_drift =
      sar::platform::wasapi_clock_position_to_audio_frames(
          0, 384000, 48000, baseline_frames) &&
      sar::platform::wasapi_clock_position_to_audio_frames(
          384000, 384000, 48000, converted_frames)
          ? sar::realtime::ClockDriftEstimator::estimate(
                {clock_domain, baseline_frames, 0},
                {clock_domain, converted_frames, 10000000})
          : sar::realtime::ClockDriftEstimate{};
  if (const auto failure = expect(
          converted_drift.valid &&
              converted_drift.observed_sample_rate == 48000.0,
          "Expected converted IAudioClock drift rate in audio frames per second")) {
    return failure;
  }
  if (const auto failure = expect(
          !sar::platform::wasapi_clock_position_to_audio_frames(
              1, 0, 48000, converted_frames) &&
              converted_frames == 0,
          "Expected zero IAudioClock frequency rejected")) {
    return failure;
  }
  if (const auto failure = expect(
          !sar::platform::wasapi_clock_position_to_audio_frames(
              std::numeric_limits<std::uint64_t>::max(), 1, 48000,
              converted_frames) &&
              converted_frames == 0,
          "Expected overflowing IAudioClock conversion rejected")) {
    return failure;
  }

  {
    constexpr std::uint32_t capture_rate = 44100;
    constexpr std::uint32_t capture_quantum = 970;
    constexpr std::uint32_t render_rate = 48000;
    constexpr std::uint32_t render_quantum = 1056;
    sar::platform::WasapiDuplexClockFeedForwardEstimator estimator(
        capture_rate, capture_quantum, render_rate, render_quantum);
    if (const auto failure = expect(
            estimator.minimum_window_duration_100ns() > 200'000'000 &&
                estimator.minimum_window_duration_100ns() < 230'000'000,
            "Expected quantization-derived duplex clock window near 22 seconds")) {
      return failure;
    }

    const sar::platform::WasapiClockSnapshot capture_anchor{
        1, 1, capture_rate};
    const sar::platform::WasapiClockSnapshot render_anchor{
        1, 1, render_rate};
    if (const auto failure = expect(
            estimator.observe(capture_anchor, render_anchor).status ==
                sar::platform::WasapiDuplexClockObservationStatus::WarmingUp,
            "Expected first duplex clock sample to establish an anchor")) {
      return failure;
    }

    constexpr std::uint64_t short_window_100ns = 5'000'000;
    const auto short_observation = estimator.observe(
        {1 + capture_rate / 2 + capture_quantum,
         1 + short_window_100ns,
         capture_rate},
        {1 + render_rate / 2 - render_quantum,
         1 + short_window_100ns,
         render_rate});
    if (const auto failure = expect(
            short_observation.status ==
                sar::platform::WasapiDuplexClockObservationStatus::WarmingUp,
            "Expected 500ms packet-quantized sample to remain warming up")) {
      return failure;
    }

    constexpr std::uint64_t long_window_seconds = 22;
    constexpr std::uint64_t long_window_100ns =
        long_window_seconds * 10'000'000;
    const auto long_observation = estimator.observe(
        {1 + capture_rate * long_window_seconds + capture_quantum,
         1 + long_window_100ns,
         capture_rate},
        {1 + render_rate * long_window_seconds - render_quantum,
         1 + long_window_100ns,
         render_rate});
    if (const auto failure = expect(
            long_observation.status ==
                    sar::platform::WasapiDuplexClockObservationStatus::Ready &&
                long_observation.feed_forward.valid &&
                std::abs(long_observation.feed_forward.correction_ppm) <=
                    2500.0,
            "Expected long anchor window to bound 970/1056-frame quantization")) {
      return failure;
    }

    constexpr std::uint64_t maximum_window_seconds = 60;
    constexpr std::uint64_t maximum_window_100ns =
        maximum_window_seconds * 10'000'000;
    const auto rolling_observation = estimator.observe(
        {1 + capture_rate * maximum_window_seconds,
         1 + maximum_window_100ns,
         capture_rate},
        {1 + render_rate * maximum_window_seconds,
         1 + maximum_window_100ns,
         render_rate});
    if (const auto failure = expect(
            rolling_observation.status ==
                sar::platform::WasapiDuplexClockObservationStatus::Ready,
            "Expected maximum duplex clock window to produce before rolling")) {
      return failure;
    }
    const auto post_roll_observation = estimator.observe(
        {1 + capture_rate * maximum_window_seconds + capture_rate / 2,
         1 + maximum_window_100ns + short_window_100ns,
         capture_rate},
        {1 + render_rate * maximum_window_seconds + render_rate / 2,
         1 + maximum_window_100ns + short_window_100ns,
         render_rate});
    if (const auto failure = expect(
            post_roll_observation.status ==
                sar::platform::WasapiDuplexClockObservationStatus::WarmingUp,
            "Expected rolled duplex clock anchor to warm without invalidation")) {
      return failure;
    }

    if (const auto failure = expect(
            estimator.observe(capture_anchor, render_anchor).status ==
                sar::platform::WasapiDuplexClockObservationStatus::Invalid,
            "Expected regressed duplex clocks to invalidate and re-anchor")) {
      return failure;
    }
  }

  {
    sar::graph::Graph graph(41, 2, 128, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    const auto result =
        sar::platform::open_wasapi_duplex_loop("", "", graph, diagnostics);
    if (const auto failure = expect(!result.ok(),
                                    "Expected empty explicit device ID failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_device_id_encoding"),
                                    "Expected empty device ID probe error propagation")) {
      return failure;
    }
  }

  {
    sar::graph::Graph graph(42, 2, 128, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    ScriptedDuplexOpen script;
    const std::string capture_device_id = "requested-capture-id";
    const std::string render_device_id = "requested-render-id";
    auto result = sar::platform::WindowsWasapiDuplexLoopTestAccess::open(
        &capture_device_id,
        &render_device_id,
        graph,
        diagnostics,
        scripted_probe,
        scripted_open,
        &script);
    if (const auto failure = expect(result.ok(),
                                    "Expected scripted explicit duplex construction")) {
      return failure;
    }
    if (const auto failure = expect(script.order_ok && script.next_step == 4,
                                    "Expected render-first shared duplex construction")) {
      return failure;
    }
    if (const auto failure = expect(script.requested_capture_id == capture_device_id &&
                                        script.requested_render_id == render_device_id,
                                    "Expected requested endpoint IDs passed to probes")) {
      return failure;
    }
    if (const auto failure = expect(script.render_requested_sample_rate == 0 &&
                                        script.capture_requested_sample_rate == 0,
                                    "Expected capture to open at its native sample rate")) {
      return failure;
    }
    if (const auto failure = expect(
            result.loop().capture_probe().device_id == "stable-capture-id" &&
                result.loop().render_probe().device_id == "stable-render-id",
            "Expected opened loop to retain stable probed endpoint IDs")) {
      return failure;
    }
    if (const auto failure = expect(
            result.loop().capture_probe().mix_format.sample_rate == 44100 &&
                result.loop().render_probe().mix_format.sample_rate == 48000,
            "Expected opened loop to preserve native endpoint sample rates")) {
      return failure;
    }
  }

  {
    sar::graph::Graph graph(43, 2, 128, 48000);
    sar::diagnostics::EngineDiagnostics diagnostics;
    ScriptedDuplexOpen script;
    script.fail_capture_open = true;
    auto result = sar::platform::WindowsWasapiDuplexLoopTestAccess::open(
        nullptr,
        nullptr,
        graph,
        diagnostics,
        scripted_probe,
        scripted_open,
        &script);
    if (const auto failure = expect(!result.ok() && script.render_used_default &&
                                        script.capture_used_default,
                                    "Expected default selection to use shared construction")) {
      return failure;
    }
    if (const auto failure = expect(
            has_error_code(result, "scripted_capture_open_failed") &&
                result.errors().front().native_hresult == -123 &&
                result.errors().front().native_win32_code == 456,
            "Expected shared duplex open error conversion")) {
      return failure;
    }
  }

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
  } else {
    const auto& capture_probe = capture_probe_result.probe();
    const auto& render_probe = render_probe_result.probe();
    duplex_sample_rate = render_probe.mix_format.sample_rate;
    {
      sar::graph::Graph mismatched_graph(
          33,
          std::max(capture_probe.mix_format.channels, render_probe.mix_format.channels),
          std::max(capture_probe.buffer_frames, render_probe.buffer_frames),
          mismatched_sample_rate(render_probe.mix_format.sample_rate));
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
          render_probe.mix_format.sample_rate);
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
  if (const auto failure = expect(initial_summary.frame_balance == 0,
                                  "Expected initial duplex frame balance zero")) {
    return failure;
  }
  if (const auto failure =
          expect(initial_summary.runtime.health ==
                     sar::platform::WasapiRuntimeHealth::Stopped,
                 "Expected initial duplex runtime summary stopped")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.runtime.reason_code == "no_cycles",
                                  "Expected initial duplex runtime no-cycle reason")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.runtime.capture_buffer_frames ==
                                      loop->capture_probe().buffer_frames,
                                  "Expected initial duplex runtime capture buffer size")) {
    return failure;
  }
  if (const auto failure = expect(initial_summary.runtime.render_buffer_frames ==
                                      loop->render_probe().buffer_frames,
                                  "Expected initial duplex runtime render buffer size")) {
    return failure;
  }

  if (const auto failure = expect(loop->render_probe().mix_format.sample_rate ==
                                      graph.sample_rate(),
                                  "Expected graph to match render sample rate")) {
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
  std::uint64_t expected_capture_frames = 0;
  if (const auto failure = expect(
          sar::platform::wasapi_clock_position_to_audio_frames(
              stats.captured_frames,
              loop->capture_probe().mix_format.sample_rate,
              loop->render_probe().mix_format.sample_rate,
              expected_capture_frames),
          "Expected duplex summary frame-domain conversion")) {
    return failure;
  }
  const auto expected_frame_balance =
      expected_capture_frames >= stats.rendered_frames
          ? static_cast<std::int64_t>(expected_capture_frames -
                                      stats.rendered_frames)
          : -static_cast<std::int64_t>(stats.rendered_frames -
                                       expected_capture_frames);
  if (const auto failure = expect(final_summary.frame_balance == expected_frame_balance,
                                  "Expected duplex summary frame balance")) {
    return failure;
  }
  if (const auto failure = expect(!final_summary.capture_clock_available ||
                                      final_summary.capture_clock.frequency > 0,
                                  "Expected valid capture clock frequency")) {
    return failure;
  }
  if (const auto failure = expect(!final_summary.render_clock_available ||
                                      final_summary.render_clock.frequency > 0,
                                  "Expected valid render clock frequency")) {
    return failure;
  }
  if (const auto failure = expect(!final_summary.capture_drift.valid ||
                                      (std::isfinite(final_summary.capture_drift.observed_sample_rate) &&
                                       std::isfinite(final_summary.capture_drift.nominal_error_ppm)),
                                  "Expected finite capture drift estimate")) {
    return failure;
  }
  if (const auto failure = expect(!final_summary.render_drift.valid ||
                                      (std::isfinite(final_summary.render_drift.observed_sample_rate) &&
                                       std::isfinite(final_summary.render_drift.nominal_error_ppm)),
                                  "Expected finite render drift estimate")) {
    return failure;
  }
  if (const auto failure =
          expect(final_summary.runtime.health !=
                     sar::platform::WasapiRuntimeHealth::Faulted,
                 "Expected final duplex runtime summary without fault")) {
    return failure;
  }
  if (const auto failure = expect(!final_summary.runtime.reason_code.empty(),
                                  "Expected final duplex runtime reason")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.runtime.captured_frames ==
                                      stats.captured_frames,
                                  "Expected final duplex runtime captured frames")) {
    return failure;
  }
  if (const auto failure = expect(final_summary.runtime.rendered_frames ==
                                      stats.rendered_frames,
                                  "Expected final duplex runtime rendered frames")) {
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
