#include "core/platform/windows_wasapi_realtime_worker.h"

#include "core/platform/windows_realtime_thread.h"

#include <utility>

namespace sar::platform {

namespace {

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
  processed_cycles_.store(0);
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
  return processed_cycles_.load();
}

std::vector<WasapiRealtimeWorkerError> WindowsWasapiRealtimeWorker::last_errors() const {
  std::lock_guard lock(errors_mutex_);
  return last_errors_;
}

void WindowsWasapiRealtimeWorker::run(std::uint32_t timeout_ms) noexcept {
  WindowsRealtimeThreadScope realtime_scope;
  const auto realtime_result =
      WindowsRealtimeThreadScope::enter_current_thread(realtime_scope);
  if (!realtime_result.ok()) {
    set_errors(convert_errors(realtime_result.errors()));
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
    if (result.stats().graph_processed) {
      processed_cycles_.fetch_add(1);
    }
  }

  running_.store(false);
}

void WindowsWasapiRealtimeWorker::set_errors(
    std::vector<WasapiRealtimeWorkerError> errors) {
  std::lock_guard lock(errors_mutex_);
  last_errors_ = std::move(errors);
}

}  // namespace sar::platform
