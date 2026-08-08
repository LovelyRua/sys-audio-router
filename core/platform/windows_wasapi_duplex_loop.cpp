#include "core/platform/windows_wasapi_duplex_loop.h"

#include "core/platform/windows_wasapi_loop_preflight.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Objbase.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <system_error>
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
    converted.push_back({error.code, error.message, error.native_hresult,
                         error.native_win32_code});
  }
  return converted;
}

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WasapiStreamProbeError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

WasapiStreamProbeResult probe_endpoint(const std::string* device_id,
                                       WasapiStreamDirection direction,
                                       void*) {
  return device_id == nullptr
             ? probe_default_wasapi_stream(direction)
             : probe_wasapi_stream(*device_id, direction);
}

WasapiStreamOpenResult open_endpoint(WasapiStreamProbe probe,
                                     std::uint32_t requested_sample_rate,
                                     void*) {
  return open_wasapi_stream_shell(std::move(probe), requested_sample_rate);
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

constexpr std::uint64_t kMinimumClockObservationWindow100ns = 50'000'000;
constexpr std::uint64_t kMaximumClockObservationWindow100ns = 600'000'000;
constexpr long double kClockQuantizationBudgetPpm = 2000.0L;

std::uint64_t clock_observation_window_100ns(
    std::uint32_t capture_sample_rate,
    std::uint32_t capture_quantum_frames,
    std::uint32_t render_sample_rate,
    std::uint32_t render_quantum_frames) noexcept {
  if (capture_sample_rate == 0 || render_sample_rate == 0) {
    return kMaximumClockObservationWindow100ns;
  }

  const auto quantization_seconds =
      static_cast<long double>(capture_quantum_frames) /
          static_cast<long double>(capture_sample_rate) +
      static_cast<long double>(render_quantum_frames) /
          static_cast<long double>(render_sample_rate);
  const auto required_100ns =
      quantization_seconds * 10'000'000.0L * 1'000'000.0L /
      kClockQuantizationBudgetPpm;
  if (!std::isfinite(required_100ns) ||
      required_100ns >=
          static_cast<long double>(kMaximumClockObservationWindow100ns)) {
    return kMaximumClockObservationWindow100ns;
  }

  return std::clamp(
      static_cast<std::uint64_t>(std::ceil(required_100ns)),
      kMinimumClockObservationWindow100ns,
      kMaximumClockObservationWindow100ns);
}

bool clock_snapshot_can_anchor(const WasapiClockSnapshot& snapshot) noexcept {
  return snapshot.frequency > 0 && snapshot.qpc_position_100ns > 0;
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

WasapiDuplexClockFeedForwardEstimator::WasapiDuplexClockFeedForwardEstimator(
    std::uint32_t capture_sample_rate,
    std::uint32_t capture_quantum_frames,
    std::uint32_t render_sample_rate,
    std::uint32_t render_quantum_frames) noexcept
    : capture_sample_rate_(capture_sample_rate),
      render_sample_rate_(render_sample_rate),
      minimum_window_duration_100ns_(clock_observation_window_100ns(
          capture_sample_rate,
          capture_quantum_frames,
          render_sample_rate,
          render_quantum_frames)) {}

WasapiDuplexClockObservation
WasapiDuplexClockFeedForwardEstimator::observe(
    const WasapiClockSnapshot& capture,
    const WasapiClockSnapshot& render) noexcept {
  if (!clock_snapshot_can_anchor(capture) ||
      !clock_snapshot_can_anchor(render) || capture_sample_rate_ == 0 ||
      render_sample_rate_ == 0) {
    anchor_available_ = false;
    return {WasapiDuplexClockObservationStatus::Invalid};
  }

  if (!anchor_available_) {
    set_anchor(capture, render);
    return {};
  }

  if (capture.frequency != capture_anchor_.frequency ||
      render.frequency != render_anchor_.frequency ||
      capture.position <= capture_anchor_.position ||
      render.position <= render_anchor_.position ||
      capture.qpc_position_100ns <= capture_anchor_.qpc_position_100ns ||
      render.qpc_position_100ns <= render_anchor_.qpc_position_100ns) {
    set_anchor(capture, render);
    return {WasapiDuplexClockObservationStatus::Invalid};
  }

  const auto capture_duration =
      capture.qpc_position_100ns - capture_anchor_.qpc_position_100ns;
  const auto render_duration =
      render.qpc_position_100ns - render_anchor_.qpc_position_100ns;
  const auto window_duration = std::min(capture_duration, render_duration);
  if (window_duration < minimum_window_duration_100ns_) {
    return {WasapiDuplexClockObservationStatus::WarmingUp, {},
            window_duration};
  }

  std::uint64_t capture_anchor_frames = 0;
  std::uint64_t capture_frames = 0;
  std::uint64_t render_anchor_frames = 0;
  std::uint64_t render_frames = 0;
  realtime::ClockRateFeedForward feed_forward;
  if (wasapi_clock_position_to_audio_frames(
          capture_anchor_.position, capture_anchor_.frequency,
          capture_sample_rate_, capture_anchor_frames) &&
      wasapi_clock_position_to_audio_frames(
          capture.position, capture.frequency, capture_sample_rate_,
          capture_frames) &&
      wasapi_clock_position_to_audio_frames(
          render_anchor_.position, render_anchor_.frequency,
          render_sample_rate_, render_anchor_frames) &&
      wasapi_clock_position_to_audio_frames(
          render.position, render.frequency, render_sample_rate_,
          render_frames)) {
    const realtime::ClockDomain capture_domain{1, capture_sample_rate_};
    const realtime::ClockDomain render_domain{2, render_sample_rate_};
    const auto capture_drift = realtime::ClockDriftEstimator::estimate(
        {capture_domain, capture_anchor_frames,
         capture_anchor_.qpc_position_100ns},
        {capture_domain, capture_frames, capture.qpc_position_100ns});
    const auto render_drift = realtime::ClockDriftEstimator::estimate(
        {render_domain, render_anchor_frames,
         render_anchor_.qpc_position_100ns},
        {render_domain, render_frames, render.qpc_position_100ns});
    feed_forward = realtime::ClockDriftEstimator::relative_rate_correction(
        capture_drift, render_drift);
  }

  if (window_duration >= kMaximumClockObservationWindow100ns) {
    set_anchor(capture, render);
  }
  return {feed_forward.valid ? WasapiDuplexClockObservationStatus::Ready
                             : WasapiDuplexClockObservationStatus::Invalid,
          feed_forward, window_duration};
}

std::uint64_t
WasapiDuplexClockFeedForwardEstimator::minimum_window_duration_100ns()
    const noexcept {
  return minimum_window_duration_100ns_;
}

void WasapiDuplexClockFeedForwardEstimator::set_anchor(
    const WasapiClockSnapshot& capture,
    const WasapiClockSnapshot& render) noexcept {
  capture_anchor_ = capture;
  render_anchor_ = render;
  anchor_available_ = true;
}

WindowsWasapiDuplexLoop::~WindowsWasapiDuplexLoop() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiDuplexLoop::start(std::uint32_t timeout_ms) {
  stop_clock_observer();
  runner_.set_capture_clock_feed_forward_ppm(0.0);
  capture_clock_feed_forward_valid_.store(false, std::memory_order_relaxed);
  capture_clock_baseline_available_ = false;
  render_clock_baseline_available_ = false;
  const auto result = worker_.start(timeout_ms);
  if (result.ok()) {
    render_clock_baseline_available_ =
        render_stream_.read_clock(render_clock_baseline_);
    establish_capture_clock_baseline(timeout_ms);
    if (capture_clock_baseline_available_ && render_clock_baseline_available_) {
      try {
        {
          std::lock_guard lock(clock_observer_mutex_);
          clock_observer_stop_requested_ = false;
        }
        clock_observer_thread_ =
            std::thread(&WindowsWasapiDuplexLoop::run_clock_observer, this);
      } catch (const std::system_error& error) {
        worker_.stop();
        return WasapiRealtimeWorkerResult::failure({{
            "clock_observer_thread_start_failed",
            "WASAPI clock observer thread creation failed: " +
                std::string(error.what()),
        }});
      }
    }
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
  stop_clock_observer();
  worker_.stop();
}

void WindowsWasapiDuplexLoop::stop_clock_observer() noexcept {
  {
    std::lock_guard lock(clock_observer_mutex_);
    clock_observer_stop_requested_ = true;
  }
  clock_observer_condition_.notify_one();
  if (clock_observer_thread_.joinable()) {
    clock_observer_thread_.join();
  }
}

void WindowsWasapiDuplexLoop::run_clock_observer() noexcept {
  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const auto com_initialized = com_result == S_OK || com_result == S_FALSE;
  if (!com_initialized && com_result != RPC_E_CHANGED_MODE) {
    runner_.set_capture_clock_feed_forward_ppm(0.0);
    capture_clock_feed_forward_valid_.store(false, std::memory_order_relaxed);
    return;
  }

  WasapiDuplexClockFeedForwardEstimator estimator(
      capture_probe().mix_format.sample_rate,
      capture_probe().buffer_frames,
      render_probe().mix_format.sample_rate,
      render_probe().buffer_frames);
  static_cast<void>(
      estimator.observe(capture_clock_baseline_, render_clock_baseline_));
  double smoothed_correction_ppm = 0.0;
  std::uint32_t consecutive_invalid_samples = 0;

  std::unique_lock lock(clock_observer_mutex_);
  while (!clock_observer_condition_.wait_for(
      lock,
      std::chrono::milliseconds(500),
      [this] { return clock_observer_stop_requested_ || !running(); })) {
    lock.unlock();

    WasapiClockSnapshot capture;
    WasapiClockSnapshot render;
    const auto capture_available = capture_stream_.read_clock(capture);
    const auto render_available = render_stream_.read_clock(render);
    auto observation = WasapiDuplexClockObservation{
        WasapiDuplexClockObservationStatus::Invalid};
    if (capture_available && render_available) {
      observation = estimator.observe(capture, render);
    }

    if (observation.status == WasapiDuplexClockObservationStatus::Ready &&
        std::abs(observation.feed_forward.correction_ppm) <= 2500.0) {
      constexpr double kSmoothingFactor = 0.2;
      constexpr double kMaximumSlewPerObservationPpm = 125.0;
      const auto filtered_correction_ppm =
          smoothed_correction_ppm +
          kSmoothingFactor *
              (observation.feed_forward.correction_ppm -
               smoothed_correction_ppm);
      const auto correction_step = std::clamp(
          filtered_correction_ppm - smoothed_correction_ppm,
          -kMaximumSlewPerObservationPpm,
          kMaximumSlewPerObservationPpm);
      smoothed_correction_ppm += correction_step;
      consecutive_invalid_samples = 0;
      runner_.set_capture_clock_feed_forward_ppm(smoothed_correction_ppm);
      capture_clock_feed_forward_valid_.store(true, std::memory_order_relaxed);
    } else if (observation.status !=
                   WasapiDuplexClockObservationStatus::WarmingUp &&
               ++consecutive_invalid_samples >= 3) {
      runner_.set_capture_clock_feed_forward_ppm(0.0);
      capture_clock_feed_forward_valid_.store(false, std::memory_order_relaxed);
      smoothed_correction_ppm = 0.0;
    }

    lock.lock();
  }

  if (com_initialized) {
    CoUninitialize();
  }
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
  result.capture_clock_feed_forward_ppm =
      runner_.capture_clock_feed_forward_ppm();
  result.capture_clock_feed_forward_valid =
      capture_clock_feed_forward_valid_.load(std::memory_order_relaxed);
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
  std::uint64_t capture_frames_in_render_domain = 0;
  if (wasapi_clock_position_to_audio_frames(
          result.worker.captured_frames,
          capture_probe().mix_format.sample_rate,
          render_probe().mix_format.sample_rate,
          capture_frames_in_render_domain)) {
    result.frame_balance = signed_frame_balance(
        capture_frames_in_render_domain, result.worker.rendered_frames);
  }
  return result;
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiDuplexLoop::last_errors() const {
  return worker_.last_errors();
}

WindowsWasapiDuplexLoop::WindowsWasapiDuplexLoop(
    WindowsWasapiStream capture_stream,
    WindowsWasapiStream render_stream,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input)
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
              graph.frames() +
                  2 * static_cast<std::size_t>(
                          std::max(capture_stream_.probe().buffer_frames,
                                   render_stream_.probe().buffer_frames)),
              true,
              true,
              external_input),
      worker_(runner_, graph, diagnostics) {}

WasapiDuplexLoopOpenResult WindowsWasapiDuplexLoop::open(
    const std::string* capture_device_id,
    const std::string* render_device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    ProbeStreamFunction probe_stream,
    OpenStreamFunction open_stream,
    void* context,
    RealtimeAudioSource* external_input) {
  auto render_probe_result =
      probe_stream(render_device_id, WasapiStreamDirection::Render, context);
  if (!render_probe_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(
        convert_errors(render_probe_result.errors()));
  }

  auto render_result =
      open_stream(render_probe_result.probe(), 0, context);
  if (!render_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(render_result.errors()));
  }

  auto capture_probe_result =
      probe_stream(capture_device_id, WasapiStreamDirection::Capture, context);
  if (!capture_probe_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(
        convert_errors(capture_probe_result.errors()));
  }

  auto capture_result = open_stream(capture_probe_result.probe(), 0, context);
  if (!capture_result.ok()) {
    return WasapiDuplexLoopOpenResult::failure(convert_errors(capture_result.errors()));
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
                                  diagnostics,
                                  external_input));
  return WasapiDuplexLoopOpenResult::success(std::move(loop));
}

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
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input) {
  return WindowsWasapiDuplexLoop::open(
      nullptr, nullptr, graph, diagnostics, probe_endpoint, open_endpoint, nullptr,
      external_input);
}

WasapiDuplexLoopOpenResult open_wasapi_duplex_loop(
    const std::string& capture_device_id,
    const std::string& render_device_id,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    RealtimeAudioSource* external_input) {
  return WindowsWasapiDuplexLoop::open(&capture_device_id,
                                       &render_device_id,
                                       graph,
                                       diagnostics,
                                       probe_endpoint,
                                       open_endpoint,
                                       nullptr,
                                       external_input);
}

}  // namespace sar::platform
