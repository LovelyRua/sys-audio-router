#include "core/platform/windows_wasapi_duplex_supervisor.h"

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_duplex_loop.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sar::platform {

namespace {

constexpr auto kAudioClientDeviceInvalidated =
    static_cast<std::int32_t>(0x88890004U);

}  // namespace

WasapiDuplexRuntimeOpenResult WasapiDuplexRuntimeOpenResult::success(
    std::unique_ptr<WasapiDuplexRuntime> runtime) {
  return {std::move(runtime), {}};
}

WasapiDuplexRuntimeOpenResult WasapiDuplexRuntimeOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors)};
}

bool WasapiDuplexRuntimeOpenResult::ok() const noexcept {
  return runtime_ != nullptr && errors_.empty();
}

std::unique_ptr<WasapiDuplexRuntime>
WasapiDuplexRuntimeOpenResult::take_runtime() noexcept {
  return std::move(runtime_);
}

const std::vector<WasapiRealtimeWorkerError>&
WasapiDuplexRuntimeOpenResult::errors() const noexcept {
  return errors_;
}

WasapiDuplexRuntimeOpenResult::WasapiDuplexRuntimeOpenResult(
    std::unique_ptr<WasapiDuplexRuntime> runtime,
    std::vector<WasapiRealtimeWorkerError> errors)
    : runtime_(std::move(runtime)), errors_(std::move(errors)) {}

WasapiFailureClass classify_wasapi_failures(
    const std::vector<WasapiRealtimeWorkerError>& errors) noexcept {
  if (errors.empty()) {
    return WasapiFailureClass::Unknown;
  }
  auto combined = WasapiFailureClass::Transient;
  for (const auto& error : errors) {
    const auto failure_class =
        error.native_hresult == kAudioClientDeviceInvalidated
            ? WasapiFailureClass::DeviceInvalidated
            : classify_wasapi_failure_code(error.code);
    if (failure_class == WasapiFailureClass::Fatal) {
      return WasapiFailureClass::Fatal;
    }
    if (failure_class == WasapiFailureClass::Unknown) {
      combined = WasapiFailureClass::Unknown;
    } else if (failure_class == WasapiFailureClass::DeviceInvalidated &&
               combined != WasapiFailureClass::Unknown) {
      combined = WasapiFailureClass::DeviceInvalidated;
    }
  }
  return combined;
}

WindowsWasapiDuplexSupervisor::WindowsWasapiDuplexSupervisor(
    WasapiDuplexRuntimeFactory factory, std::uint32_t timeout_ms)
    : factory_(std::move(factory)), timeout_ms_(timeout_ms) {
  if (!factory_) {
    throw std::invalid_argument("WASAPI duplex supervisor requires a factory");
  }
}

WindowsWasapiDuplexSupervisor::WindowsWasapiDuplexSupervisor(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms)
    : WindowsWasapiDuplexSupervisor(
          [&graph, &diagnostics] {
            auto result = open_default_wasapi_duplex_loop(graph, diagnostics);
            if (!result.ok()) {
              return WasapiDuplexRuntimeOpenResult::failure(result.errors());
            }
            return WasapiDuplexRuntimeOpenResult::success(result.take_loop());
          },
          timeout_ms) {}

WindowsWasapiDuplexSupervisor::~WindowsWasapiDuplexSupervisor() { stop(0); }

void WindowsWasapiDuplexSupervisor::start(std::uint64_t now_ms) {
  policy_.request_start(now_ms);
  if (policy_.state() == WasapiRecoveryState::Opening) {
    attempt_open(now_ms);
  }
}

void WindowsWasapiDuplexSupervisor::tick(std::uint64_t now_ms) {
  if (policy_.state() == WasapiRecoveryState::Running &&
      (!runtime_ || !runtime_->running())) {
    auto errors = runtime_ ? runtime_->last_errors()
                           : std::vector<WasapiRealtimeWorkerError>{};
    handle_failure(std::move(errors), now_ms);
    if (policy_.state() == WasapiRecoveryState::Quiescing) {
      quiesce(now_ms);
    } else if (runtime_) {
      runtime_->stop();
      runtime_.reset();
    }
  }
  policy_.tick(now_ms);
  if (policy_.state() == WasapiRecoveryState::Faulted &&
      recovery_episode_active_) {
    ++failed_recovery_count_;
    recovery_episode_active_ = false;
  }
  if (policy_.state() == WasapiRecoveryState::Opening) {
    attempt_open(now_ms);
  }
}

void WindowsWasapiDuplexSupervisor::request_reopen(std::uint64_t now_ms) {
  if (policy_.state() != WasapiRecoveryState::Running) {
    return;
  }
  if (!recovery_episode_active_) {
    recovery_episode_active_ = true;
    recovery_started_at_ms_ = now_ms;
    ++recovery_episode_count_;
  }
  last_errors_.clear();
  policy_.on_failure(WasapiFailureClass::DeviceInvalidated, now_ms);
  if (policy_.state() == WasapiRecoveryState::Quiescing) {
    quiesce(now_ms);
  }
}

void WindowsWasapiDuplexSupervisor::stop(std::uint64_t now_ms) noexcept {
  policy_.request_stop();
  recovery_episode_active_ = false;
  if (policy_.state() == WasapiRecoveryState::Quiescing) {
    quiesce(now_ms);
  } else if (runtime_) {
    runtime_->stop();
    runtime_.reset();
  }
}

WasapiRecoveryState WindowsWasapiDuplexSupervisor::state() const noexcept {
  return policy_.state();
}

bool WindowsWasapiDuplexSupervisor::running() const noexcept {
  return policy_.state() == WasapiRecoveryState::Running && runtime_ &&
         runtime_->running();
}

WasapiDuplexSupervisorSummary WindowsWasapiDuplexSupervisor::summary() const noexcept {
  return {.state = policy_.state(),
          .attempt_count = policy_.attempt_count(),
          .next_attempt_at_ms = policy_.next_attempt_at_ms(),
          .recovery_deadline_at_ms = policy_.recovery_deadline_at_ms(),
          .error_count = last_errors_.size(),
          .runtime_open_count = runtime_open_count_,
          .recovery_episode_count = recovery_episode_count_,
          .successful_recovery_count = successful_recovery_count_,
          .failed_recovery_count = failed_recovery_count_,
          .last_recovery_duration_ms = last_recovery_duration_ms_,
          .maximum_recovery_duration_ms = maximum_recovery_duration_ms_,
          .running = running()};
}

const std::vector<WasapiRealtimeWorkerError>&
WindowsWasapiDuplexSupervisor::last_errors() const noexcept {
  return last_errors_;
}

void WindowsWasapiDuplexSupervisor::attempt_open(std::uint64_t now_ms) {
  ++runtime_open_count_;
  auto open_result = factory_();
  if (!open_result.ok()) {
    handle_failure(open_result.errors(), now_ms);
    return;
  }
  auto runtime = open_result.take_runtime();
  auto start_result = runtime->start(timeout_ms_);
  if (!start_result.ok()) {
    runtime->stop();
    handle_failure(start_result.errors(), now_ms);
    return;
  }
  runtime_ = std::move(runtime);
  last_errors_.clear();
  policy_.on_open_succeeded(now_ms);
  if (recovery_episode_active_) {
    last_recovery_duration_ms_ = now_ms >= recovery_started_at_ms_
                                     ? now_ms - recovery_started_at_ms_
                                     : 0;
    maximum_recovery_duration_ms_ =
        std::max(maximum_recovery_duration_ms_, last_recovery_duration_ms_);
    ++successful_recovery_count_;
    recovery_episode_active_ = false;
  }
}

void WindowsWasapiDuplexSupervisor::handle_failure(
    std::vector<WasapiRealtimeWorkerError> errors, std::uint64_t now_ms) {
  const auto failure_class = classify_wasapi_failures(errors);
  last_errors_ = std::move(errors);
  if (wasapi_failure_is_recoverable(failure_class) &&
      !recovery_episode_active_) {
    recovery_episode_active_ = true;
    recovery_started_at_ms_ = now_ms;
    ++recovery_episode_count_;
  }
  policy_.on_failure(failure_class, now_ms);
  if (policy_.state() == WasapiRecoveryState::Faulted &&
      recovery_episode_active_) {
    ++failed_recovery_count_;
    recovery_episode_active_ = false;
  }
}

void WindowsWasapiDuplexSupervisor::quiesce(std::uint64_t now_ms) noexcept {
  if (runtime_) {
    runtime_->stop();
    runtime_.reset();
  }
  policy_.on_quiesced(now_ms);
}

}  // namespace sar::platform
