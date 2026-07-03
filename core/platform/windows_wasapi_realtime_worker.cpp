#include "core/platform/windows_wasapi_realtime_worker.h"

#include "core/platform/windows_realtime_thread.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <objbase.h>

#include <cstdio>
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
  captured_frames_.store(0);
  rendered_frames_.store(0);
  set_errors({});
  running_.store(true);
  worker_ = std::thread([this, timeout_ms] {
    run(timeout_ms);
  });

  return WasapiRealtimeWorkerResult::success();
}

void WindowsWasapiRealtimeWorker::stop() noexcept {
  stop_requested_.store(true);
  if (worker_.joinable()) {
    worker_.join();
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
  result.captured_frames = captured_frames_.load();
  result.rendered_frames = rendered_frames_.load();
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
    return;
  }

  WindowsRealtimeThreadScope realtime_scope;
  const auto realtime_result =
      WindowsRealtimeThreadScope::enter_current_thread(realtime_scope);
  if (!realtime_result.ok()) {
    set_errors(convert_errors(realtime_result.errors()));
    running_.store(false);
    return;
  }

  const auto start_result = runner_.start_streams();
  if (!start_result.ok()) {
    set_errors(convert_errors(start_result.errors()));
    running_.store(false);
    return;
  }

  while (!stop_requested_.load()) {
    auto result = runner_.process_once(graph_, diagnostics_, timeout_ms);
    if (!result.ok()) {
      set_errors(convert_errors(result.errors()));
      stop_requested_.store(true);
      break;
    }
    loop_cycles_.fetch_add(1);
    captured_frames_.fetch_add(result.stats().captured_frames);
    rendered_frames_.fetch_add(result.stats().rendered_frames);
    if (result.stats().graph_processed) {
      graph_processed_cycles_.fetch_add(1);
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
  }

  const auto stop_result = runner_.stop_streams();
  if (!stop_result.ok() && last_errors().empty()) {
    set_errors(convert_errors(stop_result.errors()));
  }

  running_.store(false);
}

void WindowsWasapiRealtimeWorker::set_errors(
    std::vector<WasapiRealtimeWorkerError> errors) {
  std::lock_guard lock(errors_mutex_);
  last_errors_ = std::move(errors);
}

}  // namespace sar::platform
