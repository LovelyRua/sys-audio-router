#include "core/platform/windows_wasapi_duplex_loop.h"

#include "core/platform/windows_wasapi_loop_preflight.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>
#include <utility>

namespace sar::platform {

namespace {

std::uint64_t fractional_audio_frames(std::uint64_t clock_remainder,
                                      std::uint64_t clock_frequency,
                                      std::uint32_t sample_rate) noexcept {
  std::uint64_t quotient = 0;
  std::uint64_t remainder = 0;
  for (std::uint32_t bit = 1U << 31U; bit != 0; bit >>= 1U) {
    const bool doubled_overflow = remainder >= clock_frequency - remainder;
    remainder = doubled_overflow ? remainder - (clock_frequency - remainder)
                                 : remainder + remainder;
    quotient = quotient * 2 + static_cast<std::uint64_t>(doubled_overflow);

    if ((sample_rate & bit) == 0) {
      continue;
    }
    if (remainder >= clock_frequency - clock_remainder) {
      remainder -= clock_frequency - clock_remainder;
      ++quotient;
    } else {
      remainder += clock_remainder;
    }
  }
  return quotient;
}

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

std::int64_t signed_frame_balance(std::uint64_t captured,
                                  std::uint64_t rendered) noexcept {
  constexpr auto maximum =
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if (captured >= rendered) {
    const auto difference = captured - rendered;
    return difference > maximum ? std::numeric_limits<std::int64_t>::max()
                                : static_cast<std::int64_t>(difference);
  }

  const auto difference = rendered - captured;
  return difference > maximum ? std::numeric_limits<std::int64_t>::min()
                              : -static_cast<std::int64_t>(difference);
}

}  // namespace

bool wasapi_clock_position_to_audio_frames(std::uint64_t position,
                                           std::uint64_t frequency,
                                           std::uint32_t sample_rate,
                                           std::uint64_t& audio_frames) noexcept {
  audio_frames = 0;
  if (frequency == 0 || sample_rate == 0) {
    return false;
  }

  const auto whole_seconds = position / frequency;
  if (whole_seconds >
      std::numeric_limits<std::uint64_t>::max() / sample_rate) {
    return false;
  }
  const auto whole_frames = whole_seconds * sample_rate;
  const auto partial_frames = fractional_audio_frames(
      position % frequency, frequency, sample_rate);
  if (whole_frames >
      std::numeric_limits<std::uint64_t>::max() - partial_frames) {
    return false;
  }
  audio_frames = whole_frames + partial_frames;
  return true;
}

bool wasapi_capture_clock_baseline_is_trustworthy(
    std::uint64_t captured_frames,
    const WasapiClockSnapshot& snapshot) noexcept {
  return captured_frames > 0 && snapshot.frequency > 0 &&
         snapshot.qpc_position_100ns > 0;
}

WindowsWasapiDuplexLoop::~WindowsWasapiDuplexLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiDuplexLoop::start(std::uint32_t timeout_ms) {
  capture_clock_baseline_available_ = false;
  render_clock_baseline_available_ = false;
  const auto result = worker_.start(timeout_ms);
  if (result.ok()) {
    render_clock_baseline_available_ =
        render_stream_.read_clock(render_clock_baseline_);
    establish_capture_clock_baseline(timeout_ms);
  }
  return result;
}

void WindowsWasapiDuplexLoop::establish_capture_clock_baseline(
    std::uint32_t timeout_ms) noexcept {
  constexpr auto minimum_wait = std::chrono::milliseconds(100);
  constexpr auto maximum_wait = std::chrono::milliseconds(500);
  const auto requested_wait = std::chrono::milliseconds(
      static_cast<std::uint64_t>(timeout_ms) * 2U);
  const auto wait = std::clamp(requested_wait, minimum_wait, maximum_wait);
  const auto deadline = std::chrono::steady_clock::now() + wait;

  do {
    const auto captured_frames = worker_.stats().captured_frames;
    if (captured_frames > 0) {
      WasapiClockSnapshot candidate;
      if (capture_stream_.read_clock(candidate) &&
          wasapi_capture_clock_baseline_is_trustworthy(captured_frames,
                                                       candidate)) {
        capture_clock_baseline_ = candidate;
        capture_clock_baseline_available_ = true;
        return;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (running() && std::chrono::steady_clock::now() < deadline);
}

void WindowsWasapiDuplexLoop::stop() noexcept {
  worker_.stop();
}

bool WindowsWasapiDuplexLoop::running() const noexcept {
  return worker_.running();
}

const WasapiStreamProbe& WindowsWasapiDuplexLoop::capture_probe() const noexcept {
  return capture_stream_.probe();
}

const WasapiStreamProbe& WindowsWasapiDuplexLoop::render_probe() const noexcept {
  return render_stream_.probe();
}

WasapiStreamDiagnostics WindowsWasapiDuplexLoop::capture_diagnostics() const noexcept {
  return capture_stream_.diagnostics();
}

WasapiStreamDiagnostics WindowsWasapiDuplexLoop::render_diagnostics() const noexcept {
  return render_stream_.diagnostics();
}

WasapiRealtimeWorkerStats WindowsWasapiDuplexLoop::stats() const noexcept {
  return worker_.stats();
}

WasapiDuplexLoopSummary WindowsWasapiDuplexLoop::summary() const {
  WasapiDuplexLoopSummary result;
  const auto errors = last_errors();
  result.running = running();
  result.error_count = errors.size();
  result.capture_stream = capture_diagnostics();
  result.render_stream = render_diagnostics();
  result.worker = stats();
  result.runtime = summarize_wasapi_runtime(
      result.worker, errors, &result.capture_stream, &result.render_stream,
      &diagnostics_);
  result.capture_clock_available = capture_stream_.read_clock(result.capture_clock);
  result.render_clock_available = render_stream_.read_clock(result.render_clock);
  if (capture_clock_baseline_available_ && result.capture_clock_available) {
    const realtime::ClockDomain domain{1, capture_probe().mix_format.sample_rate};
    std::uint64_t baseline_frames = 0;
    std::uint64_t current_frames = 0;
    if (wasapi_clock_position_to_audio_frames(capture_clock_baseline_.position,
                                              capture_clock_baseline_.frequency,
                                              domain.nominal_sample_rate,
                                              baseline_frames) &&
        wasapi_clock_position_to_audio_frames(result.capture_clock.position,
                                              result.capture_clock.frequency,
                                              domain.nominal_sample_rate,
                                              current_frames)) {
      result.capture_drift = realtime::ClockDriftEstimator::estimate(
          {domain, baseline_frames, capture_clock_baseline_.qpc_position_100ns},
          {domain, current_frames, result.capture_clock.qpc_position_100ns});
    }
  }
  if (render_clock_baseline_available_ && result.render_clock_available) {
    const realtime::ClockDomain domain{2, render_probe().mix_format.sample_rate};
    std::uint64_t baseline_frames = 0;
    std::uint64_t current_frames = 0;
    if (wasapi_clock_position_to_audio_frames(render_clock_baseline_.position,
                                              render_clock_baseline_.frequency,
                                              domain.nominal_sample_rate,
                                              baseline_frames) &&
        wasapi_clock_position_to_audio_frames(result.render_clock.position,
                                              result.render_clock.frequency,
                                              domain.nominal_sample_rate,
                                              current_frames)) {
      result.render_drift = realtime::ClockDriftEstimator::estimate(
          {domain, baseline_frames, render_clock_baseline_.qpc_position_100ns},
          {domain, current_frames, result.render_clock.qpc_position_100ns});
    }
  }
  result.frame_balance =
      signed_frame_balance(result.worker.captured_frames, result.worker.rendered_frames);
  return result;
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiDuplexLoop::last_errors() const {
  return worker_.last_errors();
}

WindowsWasapiDuplexLoop::WindowsWasapiDuplexLoop(
    WindowsWasapiStream capture_stream,
    WindowsWasapiStream render_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics)
    : capture_stream_(std::move(capture_stream)),
      render_stream_(std::move(render_stream)),
      diagnostics_(diagnostics),
      runner_(&capture_stream_,
              &render_stream_,
              capture_stream_.probe().mix_format.channels,
              render_stream_.probe().mix_format.channels,
              graph.frames(),
              capture_stream_.probe().buffer_frames,
              render_stream_.probe().buffer_frames,
              graph.frames() + std::max(capture_stream_.probe().buffer_frames,
                                        render_stream_.probe().buffer_frames),
              true,
              true),
      worker_(runner_, graph, diagnostics) {}

WasapiDuplexLoopOpenResult WasapiDuplexLoopOpenResult::success(
    std::unique_ptr<WindowsWasapiDuplexLoop> loop) {
  return {std::move(loop), {}};
}

WasapiDuplexLoopOpenResult WasapiDuplexLoopOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiDuplexLoopOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiDuplexLoop& WasapiDuplexLoopOpenResult::loop() noexcept {
  return *loop_;
}

const WindowsWasapiDuplexLoop& WasapiDuplexLoopOpenResult::loop() const noexcept {
  return *loop_;
}

std::unique_ptr<WindowsWasapiDuplexLoop> WasapiDuplexLoopOpenResult::take_loop() noexcept {
  return std::move(loop_);
}

const std::vector<WasapiRealtimeWorkerError>& WasapiDuplexLoopOpenResult::errors()
    const noexcept {
  return errors_;
}

WasapiDuplexLoopOpenResult::WasapiDuplexLoopOpenResult(
    std::unique_ptr<WindowsWasapiDuplexLoop> loop,
    std::vector<WasapiRealtimeWorkerError> errors)
    : loop_(std::move(loop)), errors_(std::move(errors)) {}

WasapiDuplexLoopOpenResult open_default_wasapi_duplex_loop(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics) {
  auto render_result = open_default_wasapi_stream_shell(WasapiStreamDirection::Render);
  if (!render_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(render_result.errors()));
  }

  auto capture_result = open_default_wasapi_stream_shell(
      WasapiStreamDirection::Capture,
      WasapiStreamMode::Endpoint,
      render_result.stream().probe().mix_format.sample_rate);
  if (!capture_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(capture_result.errors()));
  }

  if (!compatible_wasapi_duplex_sample_rates(capture_result.stream().probe(),
                                             render_result.stream().probe())) {
    return WasapiDuplexLoopOpenResult::failure({
        {
            "duplex_sample_rate_mismatch",
            "Default WASAPI capture and render streams need a sample-rate adapter before duplex use.",
        },
    });
  }

  auto graph_errors =
      validate_wasapi_duplex_graph_preflight(graph,
                                            capture_result.stream().probe(),
                                            render_result.stream().probe());
  if (!graph_errors.empty()) {
    return WasapiDuplexLoopOpenResult::failure(std::move(graph_errors));
  }

  auto loop = std::unique_ptr<WindowsWasapiDuplexLoop>(
      new WindowsWasapiDuplexLoop(capture_result.take_stream(),
                                  render_result.take_stream(),
                                  graph,
                                  diagnostics));
  return WasapiDuplexLoopOpenResult::success(std::move(loop));
}

}  // namespace sar::platform
