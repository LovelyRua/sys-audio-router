#include "core/platform/windows_wasapi_realtime_worker.h"

#include "core/platform/windows_realtime_thread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <objbase.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <iterator>
#include <system_error>
#include <utility>

namespace sar::platform {

namespace {

class ComApartmentScope {
 public:
  ComApartmentScope() {
    result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  }

  ComApartmentScope(const ComApartmentScope&) = delete;
  ComApartmentScope& operator=(const ComApartmentScope&) = delete;

  ~ComApartmentScope() {
    if (result_ == S_OK || result_ == S_FALSE) {
      CoUninitialize();
    }
  }

  [[nodiscard]] HRESULT result() const noexcept {
    return result_;
  }

  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_ = E_FAIL;
};

std::string hresult_hex(HRESULT result) {
  char buffer[16] = {};
  const auto value = static_cast<unsigned long>(result);
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", value);
  return buffer;
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

std::uint64_t seconds_to_nanoseconds(double seconds) noexcept {
  if (seconds <= 0.0) {
    return 0;
  }
  return static_cast<std::uint64_t>(seconds * 1'000'000'000.0);
}

std::uint64_t double_bits(double value) noexcept {
  return std::bit_cast<std::uint64_t>(value);
}

double bits_double(std::uint64_t value) noexcept {
  return std::bit_cast<double>(value);
}

std::vector<WasapiRealtimeWorkerError> convert_errors(
    const std::vector<WindowsRealtimeThreadError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

}  // namespace

WasapiRealtimeWorkerResult WasapiRealtimeWorkerResult::success() {
  return WasapiRealtimeWorkerResult({});
}

WasapiRealtimeWorkerResult WasapiRealtimeWorkerResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return WasapiRealtimeWorkerResult(std::move(errors));
}

bool WasapiRealtimeWorkerResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<WasapiRealtimeWorkerError>& WasapiRealtimeWorkerResult::errors()
    const noexcept {
  return errors_;
}

WasapiRealtimeWorkerResult::WasapiRealtimeWorkerResult(
    std::vector<WasapiRealtimeWorkerError> errors)
    : errors_(std::move(errors)) {}

WindowsWasapiRealtimeWorker::WindowsWasapiRealtimeWorker(
    WindowsWasapiGraphRunner& runner,
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics)
    : runner_(runner), graph_(graph), diagnostics_(diagnostics) {}

WindowsWasapiRealtimeWorker::~WindowsWasapiRealtimeWorker() {
  stop();
}

WasapiRealtimeWorkerResult WindowsWasapiRealtimeWorker::start(std::uint32_t timeout_ms) {
  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  if (running_.load()) {
    return WasapiRealtimeWorkerResult::failure({
        {"worker_already_running", "WASAPI realtime worker is already running."},
    });
  }
  if (worker_.joinable()) {
    worker_.join();
  }

  stop_requested_.store(false);
  loop_cycles_.store(0);
  graph_processed_cycles_.store(0);
  idle_cycles_.store(0);
  capture_idle_cycles_.store(0);
  render_idle_cycles_.store(0);
  wait_timeout_cycles_.store(0);
  capture_wait_timeout_cycles_.store(0);
  render_wait_timeout_cycles_.store(0);
  capture_partial_cycles_.store(0);
  render_partial_cycles_.store(0);
  capture_partial_frames_.store(0);
  render_partial_frames_.store(0);
  capture_silent_cycles_.store(0);
  capture_silent_frames_.store(0);
  capture_discontinuity_cycles_.store(0);
  capture_discontinuity_frames_.store(0);
  capture_timestamp_error_cycles_.store(0);
  capture_timestamp_error_frames_.store(0);
  stream_start_error_cycles_.store(0);
  stream_stop_error_cycles_.store(0);
  stream_wait_cancellation_cycles_.store(0);
  process_error_cycles_.store(0);
  xrun_count_.store(0);
  last_callback_nanoseconds_.store(0);
  peak_callback_nanoseconds_.store(0);
  total_callback_nanoseconds_.store(0);
  captured_frames_.store(0);
  rendered_frames_.store(0);
  capture_resampler_input_frames_.store(0);
  capture_resampler_output_frames_.store(0);
  capture_rate_correction_bits_.store(double_bits(0.0));
  capture_resampler_ratio_bits_.store(double_bits(1.0));
  capture_rate_adapter_active_.store(false);
  capture_rate_adapter_recovering_.store(false);
  capture_rate_adapter_reset_cycles_.store(0);
  render_recovery_silence_cycles_.store(0);
  render_recovery_silence_frames_.store(0);
  minimum_capture_rate_correction_bits_.store(double_bits(0.0));
  maximum_capture_rate_correction_bits_.store(double_bits(0.0));
  last_captured_frames_.store(0);
  last_rendered_frames_.store(0);
  last_graph_processed_.store(false);
  last_capture_idle_.store(false);
  last_render_idle_.store(false);
  last_capture_wait_timed_out_.store(false);
  last_render_wait_timed_out_.store(false);
  last_capture_partial_.store(false);
  last_render_partial_.store(false);
  last_capture_silent_.store(false);
  last_capture_discontinuity_.store(false);
  last_capture_timestamp_error_.store(false);
  last_render_recovery_silence_.store(false);
  last_stop_wait_microseconds_.store(0);
  xrun_baseline_ = diagnostics_.xrun_count;
  {
    std::lock_guard lock(startup_mutex_);
    startup_complete_ = false;
    startup_succeeded_ = false;
  }
  set_errors({});
  running_.store(true);
  try {
    worker_ = std::thread([this, timeout_ms] {
      run(timeout_ms);
    });
  } catch (const std::system_error& error) {
    running_.store(false);
    auto errors = std::vector<WasapiRealtimeWorkerError>{
        {"worker_thread_start_failed",
         "WASAPI realtime worker thread creation failed: " +
             std::string(error.what())},
    };
    set_errors(errors);
    return WasapiRealtimeWorkerResult::failure(std::move(errors));
  }

  bool startup_succeeded = false;
  {
    std::unique_lock lock(startup_mutex_);
    startup_condition_.wait(lock, [this] { return startup_complete_; });
    startup_succeeded = startup_succeeded_;
  }
  if (!startup_succeeded) {
    if (worker_.joinable()) {
      worker_.join();
    }
    return WasapiRealtimeWorkerResult::failure(last_errors());
  }

  return WasapiRealtimeWorkerResult::success();
}

void WindowsWasapiRealtimeWorker::stop() noexcept {
  std::lock_guard lifecycle_lock(lifecycle_mutex_);
  stop_requested_.store(true);
  runner_.request_stop();
  if (worker_.joinable()) {
    const auto started = std::chrono::steady_clock::now();
    worker_.join();
    const auto stopped = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        stopped - started);
    last_stop_wait_microseconds_.store(static_cast<std::uint64_t>(elapsed.count()));
  }
}

bool WindowsWasapiRealtimeWorker::running() const noexcept {
  return running_.load();
}

std::uint64_t WindowsWasapiRealtimeWorker::processed_cycles() const noexcept {
  return graph_processed_cycles_.load();
}

WasapiRealtimeWorkerStats WindowsWasapiRealtimeWorker::stats() const noexcept {
  WasapiRealtimeWorkerStats result;
  result.loop_cycles = loop_cycles_.load();
  result.graph_processed_cycles = graph_processed_cycles_.load();
  result.idle_cycles = idle_cycles_.load();
  result.capture_idle_cycles = capture_idle_cycles_.load();
  result.render_idle_cycles = render_idle_cycles_.load();
  result.wait_timeout_cycles = wait_timeout_cycles_.load();
  result.capture_wait_timeout_cycles = capture_wait_timeout_cycles_.load();
  result.render_wait_timeout_cycles = render_wait_timeout_cycles_.load();
  result.capture_partial_cycles = capture_partial_cycles_.load();
  result.render_partial_cycles = render_partial_cycles_.load();
  result.capture_partial_frames = capture_partial_frames_.load();
  result.render_partial_frames = render_partial_frames_.load();
  result.capture_silent_cycles = capture_silent_cycles_.load();
  result.capture_silent_frames = capture_silent_frames_.load();
  result.capture_discontinuity_cycles = capture_discontinuity_cycles_.load();
  result.capture_discontinuity_frames = capture_discontinuity_frames_.load();
  result.capture_timestamp_error_cycles = capture_timestamp_error_cycles_.load();
  result.capture_timestamp_error_frames = capture_timestamp_error_frames_.load();
  result.stream_start_error_cycles = stream_start_error_cycles_.load();
  result.stream_stop_error_cycles = stream_stop_error_cycles_.load();
  result.stream_wait_cancellation_cycles = stream_wait_cancellation_cycles_.load();
  result.process_error_cycles = process_error_cycles_.load();
  result.xrun_count = xrun_count_.load();
  result.last_callback_nanoseconds = last_callback_nanoseconds_.load();
  result.peak_callback_nanoseconds = peak_callback_nanoseconds_.load();
  result.total_callback_nanoseconds = total_callback_nanoseconds_.load();
  result.captured_frames = captured_frames_.load();
  result.rendered_frames = rendered_frames_.load();
  result.capture_resampler_input_frames = capture_resampler_input_frames_.load();
  result.capture_resampler_output_frames = capture_resampler_output_frames_.load();
  result.capture_rate_correction_ppm =
      bits_double(capture_rate_correction_bits_.load());
  result.capture_resampler_ratio =
      bits_double(capture_resampler_ratio_bits_.load());
  result.capture_rate_adapter_active = capture_rate_adapter_active_.load();
  result.capture_rate_adapter_recovering =
      capture_rate_adapter_recovering_.load();
  result.capture_rate_adapter_reset_cycles =
      capture_rate_adapter_reset_cycles_.load();
  result.render_recovery_silence_cycles =
      render_recovery_silence_cycles_.load();
  result.render_recovery_silence_frames =
      render_recovery_silence_frames_.load();
  result.minimum_capture_rate_correction_ppm =
      bits_double(minimum_capture_rate_correction_bits_.load());
  result.maximum_capture_rate_correction_ppm =
      bits_double(maximum_capture_rate_correction_bits_.load());
  result.last_captured_frames = last_captured_frames_.load();
  result.last_rendered_frames = last_rendered_frames_.load();
  result.last_graph_processed = last_graph_processed_.load();
  result.last_capture_idle = last_capture_idle_.load();
  result.last_render_idle = last_render_idle_.load();
  result.last_capture_wait_timed_out = last_capture_wait_timed_out_.load();
  result.last_render_wait_timed_out = last_render_wait_timed_out_.load();
  result.last_capture_partial = last_capture_partial_.load();
  result.last_render_partial = last_render_partial_.load();
  result.last_capture_silent = last_capture_silent_.load();
  result.last_capture_discontinuity = last_capture_discontinuity_.load();
  result.last_capture_timestamp_error = last_capture_timestamp_error_.load();
  result.last_render_recovery_silence =
      last_render_recovery_silence_.load();
  result.last_stop_wait_microseconds = last_stop_wait_microseconds_.load();
  return result;
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiRealtimeWorker::last_errors() const {
  std::lock_guard lock(errors_mutex_);
  return last_errors_;
}

void WindowsWasapiRealtimeWorker::run(std::uint32_t timeout_ms) noexcept {
  ComApartmentScope com_scope;
  if (!com_scope.ok()) {
    set_errors({
        {
            "com_initialize_failed",
            "WASAPI realtime worker COM initialization failed with " +
                hresult_hex(com_scope.result()) + ".",
        },
    });
    running_.store(false);
    publish_startup_result(false);
    return;
  }

  const auto start_result = runner_.start_streams();
  if (!start_result.ok()) {
    stream_start_error_cycles_.fetch_add(1);
    set_errors(convert_errors(start_result.errors()));
    running_.store(false);
    publish_startup_result(false);
    return;
  }

  WindowsRealtimeThreadScope realtime_scope;
  const auto realtime_result =
      WindowsRealtimeThreadScope::enter_current_thread(realtime_scope);
  if (!realtime_result.ok()) {
    auto errors = convert_errors(realtime_result.errors());
    const auto rollback_result = runner_.stop_streams();
    if (!rollback_result.ok()) {
      stream_stop_error_cycles_.fetch_add(1);
      auto rollback_errors = convert_errors(rollback_result.errors());
      errors.insert(errors.end(),
                    std::make_move_iterator(rollback_errors.begin()),
                    std::make_move_iterator(rollback_errors.end()));
    }
    set_errors(std::move(errors));
    running_.store(false);
    publish_startup_result(false);
    return;
  }

  publish_startup_result(true);

  while (!stop_requested_.load()) {
    auto result = runner_.process_once(graph_, diagnostics_, timeout_ms);
    if (!result.ok()) {
      process_error_cycles_.fetch_add(1);
      set_errors(convert_errors(result.errors()));
      stop_requested_.store(true);
      break;
    }
    const auto xrun_count = diagnostics_.xrun_count;
    xrun_count_.store(xrun_count >= xrun_baseline_
                          ? xrun_count - xrun_baseline_
                          : 0);
    loop_cycles_.fetch_add(1);
    captured_frames_.fetch_add(result.stats().captured_frames);
    rendered_frames_.fetch_add(result.stats().rendered_frames);
    capture_resampler_input_frames_.fetch_add(
        result.stats().capture_resampler_input_frames);
    capture_resampler_output_frames_.fetch_add(
        result.stats().capture_resampler_output_frames);
    capture_rate_correction_bits_.store(
        double_bits(result.stats().capture_rate_correction_ppm));
    capture_resampler_ratio_bits_.store(
        double_bits(result.stats().capture_resampler_ratio));
    capture_rate_adapter_active_.store(result.stats().capture_rate_adapter_active);
    capture_rate_adapter_recovering_.store(
        result.stats().capture_rate_adapter_recovering);
    if (result.stats().capture_rate_adapter_reset) {
      capture_rate_adapter_reset_cycles_.fetch_add(1);
    }
    if (result.stats().render_recovery_silence) {
      render_recovery_silence_cycles_.fetch_add(1);
      render_recovery_silence_frames_.fetch_add(
          result.stats().render_recovery_silence_frames);
    }
    if (result.stats().capture_rate_adapter_active) {
      const auto correction = result.stats().capture_rate_correction_ppm;
      const auto minimum =
          bits_double(minimum_capture_rate_correction_bits_.load());
      const auto maximum =
          bits_double(maximum_capture_rate_correction_bits_.load());
      minimum_capture_rate_correction_bits_.store(
          double_bits(std::min(minimum, correction)));
      maximum_capture_rate_correction_bits_.store(
          double_bits(std::max(maximum, correction)));
    }
    last_captured_frames_.store(result.stats().captured_frames);
    last_rendered_frames_.store(result.stats().rendered_frames);
    last_graph_processed_.store(result.stats().graph_processed);
    last_capture_idle_.store(result.stats().capture_stream_idle);
    last_render_idle_.store(result.stats().render_stream_idle);
    last_capture_wait_timed_out_.store(result.stats().capture_wait_timed_out);
    last_render_wait_timed_out_.store(result.stats().render_wait_timed_out);
    last_capture_partial_.store(result.stats().capture_partial);
    last_render_partial_.store(result.stats().render_partial);
    last_capture_silent_.store(result.stats().capture_silent);
    last_capture_discontinuity_.store(result.stats().capture_data_discontinuity);
    last_capture_timestamp_error_.store(result.stats().capture_timestamp_error);
    last_render_recovery_silence_.store(
        result.stats().render_recovery_silence);
    if (result.stats().graph_processed) {
      graph_processed_cycles_.fetch_add(1);
      const auto last_callback_nanoseconds =
          seconds_to_nanoseconds(diagnostics_.last_callback_seconds);
      last_callback_nanoseconds_.store(last_callback_nanoseconds);
      total_callback_nanoseconds_.fetch_add(last_callback_nanoseconds);
      const auto peak_callback_nanoseconds = peak_callback_nanoseconds_.load();
      if (last_callback_nanoseconds > peak_callback_nanoseconds) {
        peak_callback_nanoseconds_.store(last_callback_nanoseconds);
      }
    }
    if (!result.stats().graph_processed || result.stats().capture_stream_idle ||
        result.stats().render_stream_idle) {
      idle_cycles_.fetch_add(1);
    }
    if (result.stats().capture_stream_idle) {
      capture_idle_cycles_.fetch_add(1);
    }
    if (result.stats().render_stream_idle) {
      render_idle_cycles_.fetch_add(1);
    }
    if (result.stats().capture_wait_timed_out) {
      capture_wait_timeout_cycles_.fetch_add(1);
    }
    if (result.stats().render_wait_timed_out) {
      render_wait_timeout_cycles_.fetch_add(1);
    }
    if (result.stats().capture_wait_timed_out || result.stats().render_wait_timed_out) {
      wait_timeout_cycles_.fetch_add(1);
    }
    if (result.stats().capture_partial) {
      capture_partial_cycles_.fetch_add(1);
      capture_partial_frames_.fetch_add(result.stats().capture_partial_frames);
    }
    if (result.stats().render_partial) {
      render_partial_cycles_.fetch_add(1);
      render_partial_frames_.fetch_add(result.stats().render_partial_frames);
    }
    if (result.stats().capture_silent) {
      capture_silent_cycles_.fetch_add(1);
      capture_silent_frames_.fetch_add(result.stats().capture_silent_frames);
    }
    if (result.stats().capture_data_discontinuity) {
      capture_discontinuity_cycles_.fetch_add(1);
      capture_discontinuity_frames_.fetch_add(result.stats().captured_frames);
    }
    if (result.stats().capture_timestamp_error) {
      capture_timestamp_error_cycles_.fetch_add(1);
      capture_timestamp_error_frames_.fetch_add(result.stats().captured_frames);
    }
    if (result.stats().cancelled) {
      stream_wait_cancellation_cycles_.fetch_add(1);
      break;
    }
  }

  realtime_scope.reset();

  const auto stop_result = runner_.stop_streams();
  if (!stop_result.ok()) {
    stream_stop_error_cycles_.fetch_add(1);
    if (last_errors().empty()) {
      set_errors(convert_errors(stop_result.errors()));
    }
  }

  running_.store(false);
}

void WindowsWasapiRealtimeWorker::publish_startup_result(bool succeeded) noexcept {
  {
    std::lock_guard lock(startup_mutex_);
    startup_succeeded_ = succeeded;
    startup_complete_ = true;
  }
  startup_condition_.notify_one();
}

void WindowsWasapiRealtimeWorker::set_errors(
    std::vector<WasapiRealtimeWorkerError> errors) {
  std::lock_guard lock(errors_mutex_);
  last_errors_ = std::move(errors);
}

}  // namespace sar::platform
