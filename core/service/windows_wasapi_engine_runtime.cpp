#include "core/service/windows_wasapi_engine_runtime.h"

#include "core/platform/windows_wasapi_endpoint_notification.h"

#include <Windows.h>

#include <chrono>
#include <system_error>
#include <utility>

namespace sar::service {

namespace {

std::vector<EngineAudioRuntimeError> convert_errors(
    const std::vector<platform::WasapiRealtimeWorkerError>& errors) {
  std::vector<EngineAudioRuntimeError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back(
        {error.code, error.message, error.native_hresult, error.native_win32_code});
  }
  return converted;
}

std::uint64_t monotonic_milliseconds() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

class ComApartment {
 public:
  ComApartment() noexcept
      : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}

  ~ComApartment() {
    if (result_ == S_OK || result_ == S_FALSE) {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_ = E_FAIL;
};

}  // namespace

WindowsWasapiEngineRuntime::~WindowsWasapiEngineRuntime() {
  stop();
}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntime::open_default_render(
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input) {
  if (!graph) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"null_runtime_graph", "WASAPI engine runtime requires a graph."},
    });
  }

  auto runtime = std::unique_ptr<WindowsWasapiEngineRuntime>(
      new WindowsWasapiEngineRuntime(std::move(graph), external_input));
  auto loop = platform::open_default_wasapi_render_loop(
      *runtime->graph_, runtime->realtime_diagnostics_, external_input);
  if (!loop.ok()) {
    return WindowsWasapiEngineRuntimeOpenResult::failure(
        convert_errors(loop.errors()));
  }
  runtime->render_loop_ = loop.take_loop();
  return WindowsWasapiEngineRuntimeOpenResult::success(std::move(runtime));
}

WindowsWasapiEngineRuntimeOpenResult WindowsWasapiEngineRuntime::open_render(
    std::string render_device_id,
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input) {
  if (!graph) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"null_runtime_graph", "WASAPI engine runtime requires a graph."},
    });
  }
  if (render_device_id.empty()) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"missing_render_device_id",
         "Explicit render runtime requires a render device ID."},
    });
  }

  auto runtime = std::unique_ptr<WindowsWasapiEngineRuntime>(
      new WindowsWasapiEngineRuntime(std::move(graph), external_input));
  auto loop = platform::open_wasapi_render_loop(
      render_device_id, *runtime->graph_, runtime->realtime_diagnostics_,
      external_input);
  if (!loop.ok()) {
    return WindowsWasapiEngineRuntimeOpenResult::failure(
        convert_errors(loop.errors()));
  }
  runtime->render_loop_ = loop.take_loop();
  return WindowsWasapiEngineRuntimeOpenResult::success(std::move(runtime));
}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntime::open_default_duplex(
    std::shared_ptr<graph::Graph> graph) {
  if (!graph) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"null_runtime_graph", "WASAPI engine runtime requires a graph."},
    });
  }

  auto runtime = std::unique_ptr<WindowsWasapiEngineRuntime>(
      new WindowsWasapiEngineRuntime(std::move(graph)));
  runtime->duplex_configured_ = true;
  return WindowsWasapiEngineRuntimeOpenResult::success(std::move(runtime));
}

WindowsWasapiEngineRuntimeOpenResult WindowsWasapiEngineRuntime::open_duplex(
    std::string capture_device_id,
    std::string render_device_id,
    std::shared_ptr<graph::Graph> graph) {
  if (!graph) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"null_runtime_graph", "WASAPI engine runtime requires a graph."},
    });
  }
  if (capture_device_id.empty() || render_device_id.empty()) {
    return WindowsWasapiEngineRuntimeOpenResult::failure({
        {"missing_duplex_device_id",
         "Explicit duplex runtime requires capture and render device IDs."},
    });
  }

  auto runtime = std::unique_ptr<WindowsWasapiEngineRuntime>(
      new WindowsWasapiEngineRuntime(std::move(graph)));
  runtime->duplex_endpoint_policy_ = platform::WasapiEndpointSelectionPolicy(
      platform::WasapiEndpointSelection::pinned_device_id(
          std::move(capture_device_id)),
      platform::WasapiEndpointSelection::pinned_device_id(
          std::move(render_device_id)));
  runtime->duplex_configured_ = true;
  return WindowsWasapiEngineRuntimeOpenResult::success(std::move(runtime));
}

EngineAudioRuntimeResult WindowsWasapiEngineRuntime::start(
    std::uint32_t timeout_ms) {
  if (!render_loop_ && !duplex_configured_) {
    return EngineAudioRuntimeResult::failure({
        {"wasapi_audio_loop_not_open", "WASAPI audio loop is not open."},
    });
  }
  if (render_loop_) {
    const auto result = render_loop_->start(timeout_ms);
    if (!result.ok()) {
      return EngineAudioRuntimeResult::failure(convert_errors(result.errors()));
    }
    return EngineAudioRuntimeResult::success();
  }

  if (duplex_supervisor_active_.load(std::memory_order_acquire)) {
    return EngineAudioRuntimeResult::failure({
        {"wasapi_duplex_supervisor_running",
         "WASAPI duplex supervisor is already running."},
    });
  }
  if (duplex_supervisor_thread_.joinable()) {
    duplex_supervisor_thread_.join();
  }
  std::lock_guard lock(duplex_supervisor_mutex_);
  duplex_supervisor_ =
      std::make_unique<platform::WindowsWasapiDuplexSupervisor>(
          *graph_, realtime_diagnostics_, timeout_ms, duplex_endpoint_policy_);
  duplex_supervisor_->start(monotonic_milliseconds());
  if (duplex_supervisor_->state() == platform::WasapiRecoveryState::Faulted) {
    auto errors = convert_errors(duplex_supervisor_->last_errors());
    duplex_supervisor_.reset();
    return EngineAudioRuntimeResult::failure(std::move(errors));
  }
  duplex_stop_requested_ = false;
  duplex_supervisor_active_.store(true, std::memory_order_release);
  try {
    duplex_supervisor_thread_ = std::thread([this] { run_duplex_supervisor(); });
  } catch (const std::system_error& error) {
    duplex_supervisor_active_.store(false, std::memory_order_release);
    duplex_supervisor_->stop(monotonic_milliseconds());
    duplex_supervisor_.reset();
    return EngineAudioRuntimeResult::failure({
        {"wasapi_supervisor_thread_start_failed", error.what()},
    });
  }
  return EngineAudioRuntimeResult::success();
}

void WindowsWasapiEngineRuntime::stop() noexcept {
  if (render_loop_) {
    render_loop_->stop();
  }
  {
    std::lock_guard lock(duplex_supervisor_mutex_);
    duplex_stop_requested_ = true;
  }
  duplex_supervisor_condition_.notify_all();
  if (duplex_supervisor_thread_.joinable()) {
    duplex_supervisor_thread_.join();
  }
  {
    std::lock_guard lock(duplex_supervisor_mutex_);
    if (duplex_supervisor_) {
      duplex_supervisor_->stop(monotonic_milliseconds());
      duplex_supervisor_.reset();
    }
    duplex_stop_requested_ = false;
  }
  duplex_supervisor_active_.store(false, std::memory_order_release);
}

bool WindowsWasapiEngineRuntime::running() const noexcept {
  return (render_loop_ && render_loop_->running()) ||
         duplex_supervisor_active_.load(std::memory_order_acquire);
}

std::uint64_t WindowsWasapiEngineRuntime::graph_version() const noexcept {
  return graph_ ? graph_->version() : 0;
}

diagnostics::EngineDiagnostics WindowsWasapiEngineRuntime::diagnostics() const {
  diagnostics::EngineDiagnostics result;
  if (!render_loop_ && !duplex_configured_) {
    return result;
  }

  const auto snapshot = runtime_summary();
  result.graph_version = graph_->version();
  result.processed_blocks = snapshot.graph_processed_cycles;
  result.xrun_count = snapshot.xrun_count;
  result.capture_fifo_fill_frames = snapshot.capture_fifo_fill_frames;
  result.render_fifo_fill_frames = snapshot.render_fifo_fill_frames;
  result.capture_fifo_overflow_cycles = snapshot.capture_fifo_overflow_cycles;
  result.capture_fifo_overflow_frames = snapshot.capture_fifo_overflow_frames;
  result.render_fifo_overflow_cycles = snapshot.render_fifo_overflow_cycles;
  result.render_fifo_overflow_frames = snapshot.render_fifo_overflow_frames;
  result.render_fifo_underflow_cycles = snapshot.render_fifo_underflow_cycles;
  result.render_fifo_underflow_frames = snapshot.render_fifo_underflow_frames;
  result.sample_conversion_import_failures =
      snapshot.sample_conversion_import_failures;
  result.sample_conversion_export_failures =
      snapshot.sample_conversion_export_failures;
  if (external_input_ != nullptr) {
    const auto input = external_input_->diagnostics();
    result.virtual_asio_pushed_blocks = input.pushed_blocks;
    result.virtual_asio_dropped_blocks = input.dropped_blocks;
    result.virtual_asio_consumed_blocks = input.consumed_blocks;
    result.virtual_asio_mixed_blocks = input.mixed_blocks;
    result.virtual_asio_silent_reads = input.silent_reads;
    result.virtual_asio_clipped_samples = input.clipped_samples;
    result.virtual_asio_non_finite_samples = input.non_finite_samples;
    result.virtual_asio_maximum_queue_depth = input.maximum_queue_depth;
    result.virtual_asio_active_producers = input.active_producers;
    result.virtual_asio_peak = input.peak;
  }
  result.last_callback_seconds =
      static_cast<double>(snapshot.last_callback_nanoseconds) / 1'000'000'000.0;
  result.peak_callback_seconds =
      static_cast<double>(snapshot.peak_callback_nanoseconds) / 1'000'000'000.0;
  return result;
}

WindowsWasapiEngineRuntimeMode WindowsWasapiEngineRuntime::mode() const noexcept {
  return duplex_configured_ ? WindowsWasapiEngineRuntimeMode::Duplex
                            : WindowsWasapiEngineRuntimeMode::Render;
}

platform::WasapiRuntimeSummary
WindowsWasapiEngineRuntime::runtime_summary() const {
  if (render_loop_) {
    return render_loop_->summary().runtime;
  }
  std::lock_guard lock(duplex_supervisor_mutex_);
  if (duplex_supervisor_) {
    return platform::summarize_wasapi_runtime(
        duplex_supervisor_->runtime_stats(),
        duplex_supervisor_->last_errors(),
        nullptr,
        nullptr);
  }
  return {};
}

void WindowsWasapiEngineRuntime::run_duplex_supervisor() noexcept {
  try {
    ComApartment apartment;
    platform::WindowsWasapiEndpointNotification notifications;
    const bool notifications_registered =
        apartment.ok() && SUCCEEDED(notifications.register_notifications());

    while (true) {
      std::unique_lock lock(duplex_supervisor_mutex_);
      if (duplex_supervisor_condition_.wait_for(
              lock,
              std::chrono::milliseconds(20),
              [this] { return duplex_stop_requested_; })) {
        break;
      }
      if (!duplex_supervisor_) {
        break;
      }

      const auto now_ms = monotonic_milliseconds();
      duplex_supervisor_->tick(now_ms);
      if (notifications_registered) {
        static_cast<void>(duplex_supervisor_->poll_endpoint_notifications(
            notifications, now_ms));
      }
      if (duplex_supervisor_->state() == platform::WasapiRecoveryState::Faulted) {
        duplex_supervisor_active_.store(false, std::memory_order_release);
        break;
      }
    }

    if (notifications_registered) {
      static_cast<void>(notifications.unregister_notifications());
    }
  } catch (...) {
    std::lock_guard lock(duplex_supervisor_mutex_);
    if (duplex_supervisor_) {
      duplex_supervisor_->stop(monotonic_milliseconds());
    }
    duplex_supervisor_active_.store(false, std::memory_order_release);
  }
}

WindowsWasapiEngineRuntime::WindowsWasapiEngineRuntime(
    std::shared_ptr<graph::Graph> graph,
    platform::RealtimeAudioSource* external_input) noexcept
    : graph_(std::move(graph)), external_input_(external_input) {}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntimeOpenResult::success(
    std::unique_ptr<WindowsWasapiEngineRuntime> runtime) {
  return {std::move(runtime), {}};
}

WindowsWasapiEngineRuntimeOpenResult
WindowsWasapiEngineRuntimeOpenResult::failure(
    std::vector<EngineAudioRuntimeError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsWasapiEngineRuntimeOpenResult::ok() const noexcept {
  return runtime_ != nullptr && errors_.empty();
}

std::unique_ptr<WindowsWasapiEngineRuntime>
WindowsWasapiEngineRuntimeOpenResult::take_runtime() noexcept {
  return std::move(runtime_);
}

const std::vector<EngineAudioRuntimeError>&
WindowsWasapiEngineRuntimeOpenResult::errors() const noexcept {
  return errors_;
}

WindowsWasapiEngineRuntimeOpenResult::WindowsWasapiEngineRuntimeOpenResult(
    std::unique_ptr<WindowsWasapiEngineRuntime> runtime,
    std::vector<EngineAudioRuntimeError> errors) noexcept
    : runtime_(std::move(runtime)), errors_(std::move(errors)) {}

}  // namespace sar::service
