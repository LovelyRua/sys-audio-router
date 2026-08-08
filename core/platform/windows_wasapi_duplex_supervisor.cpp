#include "core/platform/windows_wasapi_duplex_supervisor.h"

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_endpoint_notification.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sar::platform {

namespace {

constexpr auto kAudioClientDeviceInvalidated =
    static_cast<std::int32_t>(0x88890004U);

std::vector<WasapiRealtimeWorkerError> convert_selection_errors(
    const std::vector<WasapiEndpointSelectionError>& errors) {
  std::vector<WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

WasapiDuplexRuntimeFactory make_selected_duplex_factory(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    WasapiEndpointSelectionPolicy endpoint_selection_policy,
    RealtimeAudioSource* external_input) {
  return [&graph,
          &diagnostics,
          external_input,
          endpoint_selection_policy = std::move(endpoint_selection_policy)] {
    WindowsWasapiDeviceProvider provider;
    auto resolution =
        provider.resolve_endpoint_pair(endpoint_selection_policy);
    if (!resolution.ok()) {
      return WasapiDuplexRuntimeOpenResult::failure(
          convert_selection_errors(resolution.errors()));
    }

    const auto& selected = resolution.endpoints();
    auto result = open_wasapi_duplex_loop(selected.capture_device_id,
                                          selected.render_device_id,
                                          graph,
                                          diagnostics,
                                          external_input);
    if (!result.ok()) {
      return WasapiDuplexRuntimeOpenResult::failure(result.errors());
    }
    WasapiDuplexRuntimeEndpoints endpoints{
        .capture_device_id = result.loop().capture_probe().device_id,
        .render_device_id = result.loop().render_probe().device_id,
    };
    return WasapiDuplexRuntimeOpenResult::success(result.take_loop(),
                                                  std::move(endpoints));
  };
}

}  // namespace

WasapiDuplexRuntimeOpenResult WasapiDuplexRuntimeOpenResult::success(
    std::unique_ptr<WasapiDuplexRuntime> runtime,
    WasapiDuplexRuntimeEndpoints endpoints) {
  return {std::move(runtime), {}, std::move(endpoints)};
}

WasapiDuplexRuntimeOpenResult WasapiDuplexRuntimeOpenResult::failure(
    std::vector<WasapiRealtimeWorkerError> errors) {
  return {nullptr, std::move(errors), {}};
}

bool WasapiDuplexRuntimeOpenResult::ok() const noexcept {
  return runtime_ != nullptr && errors_.empty();
}

std::unique_ptr<WasapiDuplexRuntime>
WasapiDuplexRuntimeOpenResult::take_runtime() noexcept {
  return std::move(runtime_);
}

const WasapiDuplexRuntimeEndpoints&
WasapiDuplexRuntimeOpenResult::endpoints() const noexcept {
  return endpoints_;
}

const std::vector<WasapiRealtimeWorkerError>&
WasapiDuplexRuntimeOpenResult::errors() const noexcept {
  return errors_;
}

WasapiDuplexRuntimeOpenResult::WasapiDuplexRuntimeOpenResult(
    std::unique_ptr<WasapiDuplexRuntime> runtime,
    std::vector<WasapiRealtimeWorkerError> errors,
    WasapiDuplexRuntimeEndpoints endpoints)
    : runtime_(std::move(runtime)),
      errors_(std::move(errors)),
      endpoints_(std::move(endpoints)) {}

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
    : WindowsWasapiDuplexSupervisor(std::move(factory),
                                    timeout_ms,
                                    WasapiEndpointSelectionPolicy{}) {}

WindowsWasapiDuplexSupervisor::WindowsWasapiDuplexSupervisor(
    WasapiDuplexRuntimeFactory factory,
    std::uint32_t timeout_ms,
    WasapiEndpointSelectionPolicy endpoint_selection_policy)
    : factory_(std::move(factory)),
      timeout_ms_(timeout_ms),
      endpoint_selection_policy_(std::move(endpoint_selection_policy)) {
  if (!factory_) {
    throw std::invalid_argument("WASAPI duplex supervisor requires a factory");
  }
}

WindowsWasapiDuplexSupervisor::WindowsWasapiDuplexSupervisor(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms,
    RealtimeAudioSource* external_input)
    : WindowsWasapiDuplexSupervisor(graph,
                                    diagnostics,
                                    timeout_ms,
                                    WasapiEndpointSelectionPolicy{},
                                    external_input) {}

WindowsWasapiDuplexSupervisor::WindowsWasapiDuplexSupervisor(
    graph::Graph& graph,
    diagnostics::EngineDiagnostics& diagnostics,
    std::uint32_t timeout_ms,
    WasapiEndpointSelectionPolicy endpoint_selection_policy,
    RealtimeAudioSource* external_input)
    // Both consumers must copy the intact policy. Moving it in this delegating
    // call makes the factory argument depend on unspecified argument order.
    : WindowsWasapiDuplexSupervisor(
          make_selected_duplex_factory(graph,
                                       diagnostics,
                                       endpoint_selection_policy,
                                       external_input),
          timeout_ms,
          endpoint_selection_policy) {}

WindowsWasapiDuplexSupervisor::~WindowsWasapiDuplexSupervisor() { stop(0); }

void WindowsWasapiDuplexSupervisor::start(std::uint64_t now_ms) {
  policy_.request_start(now_ms);
  if (policy_.state() == WasapiRecoveryState::Opening) {
    attempt_open(now_ms);
  }
}

void WindowsWasapiDuplexSupervisor::tick(std::uint64_t now_ms) {
  if (endpoint_notification_reopen_pending_ &&
      policy_.state() == WasapiRecoveryState::Running &&
      now_ms >= endpoint_notification_reopen_at_ms_) {
    endpoint_notification_reopen_pending_ = false;
    endpoint_notification_reopen_at_ms_ = 0;
    ++endpoint_notification_reopen_count_;
    request_reopen(now_ms);
  }
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
      active_endpoints_.capture_device_id.clear();
      active_endpoints_.render_device_id.clear();
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

WasapiEndpointReopenRequirements
WindowsWasapiDuplexSupervisor::poll_endpoint_notifications(
    WindowsWasapiEndpointNotification& notifications,
    std::uint64_t now_ms) {
  const auto snapshot = notifications.consume_snapshot();
  if (!snapshot.event_reset_succeeded) {
    ++endpoint_notification_reset_failure_count_;
  }
  const WasapiDefaultEndpointGenerations current{
      .capture = snapshot.capture_generation,
      .render = snapshot.render_generation,
  };
  const bool generations_changed = endpoint_generations_initialized_ &&
                                   (current.capture != endpoint_generations_.capture ||
                                    current.render != endpoint_generations_.render);

  if (!endpoint_generations_initialized_) {
    endpoint_generations_ = current;
    endpoint_generations_initialized_ = true;
    endpoint_selection_policy_.mark_opened(current);
    return {};
  }

  endpoint_generations_ = current;
  const auto requirements =
      endpoint_selection_policy_.reopen_requirements(current);
  if ((requirements.capture || requirements.render) &&
      policy_.state() == WasapiRecoveryState::Running && generations_changed) {
    endpoint_notification_reopen_pending_ = true;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    endpoint_notification_reopen_at_ms_ =
        now_ms > maximum - kEndpointNotificationSettleMs
            ? maximum
            : now_ms + kEndpointNotificationSettleMs;
  } else if (!requirements.capture && !requirements.render) {
    endpoint_notification_reopen_pending_ = false;
    endpoint_notification_reopen_at_ms_ = 0;
    endpoint_selection_policy_.mark_opened(current);
  }
  return requirements;
}

void WindowsWasapiDuplexSupervisor::request_reopen(std::uint64_t now_ms) {
  if (policy_.state() != WasapiRecoveryState::Running) {
    return;
  }
  endpoint_notification_reopen_pending_ = false;
  endpoint_notification_reopen_at_ms_ = 0;
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
  endpoint_notification_reopen_pending_ = false;
  endpoint_notification_reopen_at_ms_ = 0;
  recovery_episode_active_ = false;
  if (policy_.state() == WasapiRecoveryState::Quiescing) {
    quiesce(now_ms);
  } else if (runtime_) {
    retain_runtime_diagnostics();
    runtime_->stop();
    runtime_.reset();
    active_endpoints_.capture_device_id.clear();
    active_endpoints_.render_device_id.clear();
  }
}

WasapiRecoveryState WindowsWasapiDuplexSupervisor::state() const noexcept {
  return policy_.state();
}

bool WindowsWasapiDuplexSupervisor::running() const noexcept {
  return policy_.state() == WasapiRecoveryState::Running && runtime_ &&
         runtime_->running();
}

WasapiDuplexSupervisorSummary WindowsWasapiDuplexSupervisor::summary() const {
  const auto active_maximum_render_recovery_silence_frames =
      runtime_ ? runtime_->stats().maximum_render_recovery_silence_frames : 0;
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
          .maximum_render_recovery_silence_frames = std::max(
              maximum_render_recovery_silence_frames_,
              active_maximum_render_recovery_silence_frames),
          .capture_endpoint_generation = endpoint_generations_.capture,
          .render_endpoint_generation = endpoint_generations_.render,
          .endpoint_notification_reopen_count =
              endpoint_notification_reopen_count_,
          .endpoint_notification_reset_failure_count =
              endpoint_notification_reset_failure_count_,
          .endpoint_notification_reopen_at_ms =
              endpoint_notification_reopen_at_ms_,
          .endpoint_generations_initialized =
              endpoint_generations_initialized_,
          .endpoint_notification_reopen_pending =
              endpoint_notification_reopen_pending_,
          .active_capture_device_id = active_endpoints_.capture_device_id,
          .active_render_device_id = active_endpoints_.render_device_id,
          .running = running()};
}

WasapiRealtimeWorkerStats WindowsWasapiDuplexSupervisor::runtime_stats()
    const noexcept {
  return runtime_ ? runtime_->stats() : WasapiRealtimeWorkerStats{};
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
  auto endpoints = open_result.endpoints();
  auto runtime = open_result.take_runtime();
  auto start_result = runtime->start(timeout_ms_);
  if (!start_result.ok()) {
    runtime->stop();
    handle_failure(start_result.errors(), now_ms);
    return;
  }
  runtime_ = std::move(runtime);
  active_endpoints_ = std::move(endpoints);
  last_errors_.clear();
  policy_.on_open_succeeded(now_ms);
  if (endpoint_generations_initialized_) {
    endpoint_selection_policy_.mark_opened(endpoint_generations_);
  }
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
  endpoint_notification_reopen_pending_ = false;
  endpoint_notification_reopen_at_ms_ = 0;
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

void WindowsWasapiDuplexSupervisor::retain_runtime_diagnostics() noexcept {
  if (!runtime_) {
    return;
  }
  maximum_render_recovery_silence_frames_ =
      std::max(maximum_render_recovery_silence_frames_,
               runtime_->stats().maximum_render_recovery_silence_frames);
}

void WindowsWasapiDuplexSupervisor::quiesce(std::uint64_t now_ms) noexcept {
  endpoint_notification_reopen_pending_ = false;
  endpoint_notification_reopen_at_ms_ = 0;
  if (runtime_) {
    retain_runtime_diagnostics();
    runtime_->stop();
    runtime_.reset();
  }
  active_endpoints_.capture_device_id.clear();
  active_endpoints_.render_device_id.clear();
  policy_.on_quiesced(now_ms);
}

}  // namespace sar::platform
