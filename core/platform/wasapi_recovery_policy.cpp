#include "core/platform/wasapi_recovery_policy.h"

#include <array>
#include <initializer_list>
#include <limits>

namespace sar::platform {

namespace {

constexpr std::array<std::uint64_t, WasapiRecoveryPolicy::kMaxAttempts>
    kAttemptDelaysMs = {0, 500, 3000};

std::uint64_t saturating_add(std::uint64_t value,
                             std::uint64_t increment) noexcept {
  const auto maximum = std::numeric_limits<std::uint64_t>::max();
  return increment > maximum - value ? maximum : value + increment;
}

bool is_one_of(std::string_view value,
               std::initializer_list<std::string_view> candidates) noexcept {
  for (const auto candidate : candidates) {
    if (value == candidate) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool wasapi_failure_is_recoverable(
    WasapiFailureClass failure_class) noexcept {
  switch (failure_class) {
    case WasapiFailureClass::Transient:
    case WasapiFailureClass::DeviceInvalidated:
      return true;
    case WasapiFailureClass::Fatal:
    case WasapiFailureClass::Unknown:
      return false;
  }
  return false;
}

WasapiFailureClass classify_wasapi_failure_code(
    std::string_view code) noexcept {
  if (is_one_of(code, {
                          "wasapi_device_lookup_failed",
                          "wasapi_pinned_endpoint_unavailable",
                          "wasapi_probe_format_changed",
                          "wasapi_probe_buffer_changed",
                      })) {
    return WasapiFailureClass::DeviceInvalidated;
  }

  if (code.starts_with("wasapi_") &&
      (code.ends_with("_failed") || code == "wasapi_event_wait_failed")) {
    return WasapiFailureClass::Transient;
  }

  if (code.starts_with("graph_") || code.starts_with("invalid_") ||
      code.starts_with("sample_conversion_") ||
      code.starts_with("capture_resampler_") ||
      code == "stream_not_open" || code == "stream_already_open" ||
      code == "wasapi_resample_mode_unsupported") {
    return WasapiFailureClass::Fatal;
  }

  return WasapiFailureClass::Unknown;
}

WasapiRecoveryState WasapiRecoveryPolicy::state() const noexcept {
  return state_;
}

std::uint32_t WasapiRecoveryPolicy::attempt_count() const noexcept {
  return attempt_count_;
}

std::uint64_t WasapiRecoveryPolicy::next_attempt_at_ms() const noexcept {
  return next_attempt_at_ms_;
}

std::uint64_t WasapiRecoveryPolicy::recovery_deadline_at_ms() const noexcept {
  return recovery_deadline_at_ms_;
}

void WasapiRecoveryPolicy::request_start(std::uint64_t now_ms) noexcept {
  if (state_ != WasapiRecoveryState::Stopped) {
    return;
  }

  clear_recovery();
  running_since_ms_ = now_ms;
  state_ = WasapiRecoveryState::Opening;
}

void WasapiRecoveryPolicy::request_stop() noexcept {
  switch (state_) {
    case WasapiRecoveryState::Opening:
    case WasapiRecoveryState::Running:
      stop_requested_ = true;
      recovery_pending_ = false;
      state_ = WasapiRecoveryState::Quiescing;
      return;
    case WasapiRecoveryState::Quiescing:
      stop_requested_ = true;
      recovery_pending_ = false;
      return;
    case WasapiRecoveryState::Backoff:
    case WasapiRecoveryState::Faulted:
      clear_recovery();
      state_ = WasapiRecoveryState::Stopped;
      return;
    case WasapiRecoveryState::Stopped:
      return;
  }
}

void WasapiRecoveryPolicy::on_quiesced(std::uint64_t now_ms) noexcept {
  if (state_ != WasapiRecoveryState::Quiescing) {
    return;
  }

  if (stop_requested_) {
    clear_recovery();
    state_ = WasapiRecoveryState::Stopped;
    return;
  }

  if (recovery_pending_) {
    recovery_pending_ = false;
    schedule_recovery(now_ms);
    return;
  }

  clear_recovery();
  state_ = WasapiRecoveryState::Stopped;
}

void WasapiRecoveryPolicy::on_open_succeeded(std::uint64_t now_ms) noexcept {
  if (state_ != WasapiRecoveryState::Opening) {
    return;
  }

  running_since_ms_ = now_ms;
  state_ = WasapiRecoveryState::Running;
}

void WasapiRecoveryPolicy::on_failure(WasapiFailureClass failure_class,
                                      std::uint64_t now_ms) noexcept {
  if (state_ != WasapiRecoveryState::Opening &&
      state_ != WasapiRecoveryState::Running) {
    return;
  }

  reset_if_stable(now_ms);
  if (!wasapi_failure_is_recoverable(failure_class)) {
    fault();
    return;
  }

  begin_recovery(now_ms);
  if (state_ == WasapiRecoveryState::Running) {
    recovery_pending_ = true;
    state_ = WasapiRecoveryState::Quiescing;
    return;
  }

  schedule_recovery(now_ms);
}

void WasapiRecoveryPolicy::begin_recovery(std::uint64_t now_ms) noexcept {
  if (recovery_active_) {
    return;
  }

  recovery_active_ = true;
  recovery_deadline_at_ms_ = saturating_add(now_ms, kRecoveryDeadlineMs);
}

void WasapiRecoveryPolicy::schedule_recovery(std::uint64_t now_ms) noexcept {
  if (now_ms >= recovery_deadline_at_ms_) {
    fault();
    return;
  }
  if (attempt_count_ >= kMaxAttempts) {
    fault();
    return;
  }

  const auto delay_ms = kAttemptDelaysMs[attempt_count_];
  ++attempt_count_;
  next_attempt_at_ms_ = saturating_add(now_ms, delay_ms);
  state_ = WasapiRecoveryState::Backoff;
}

void WasapiRecoveryPolicy::tick(std::uint64_t now_ms) noexcept {
  if (state_ == WasapiRecoveryState::Running) {
    reset_if_stable(now_ms);
    return;
  }

  if (state_ == WasapiRecoveryState::Opening && recovery_active_ &&
      now_ms >= recovery_deadline_at_ms_) {
    fault();
    return;
  }

  if (state_ != WasapiRecoveryState::Backoff) {
    return;
  }
  if (now_ms >= recovery_deadline_at_ms_) {
    fault();
    return;
  }
  if (now_ms >= next_attempt_at_ms_) {
    state_ = WasapiRecoveryState::Opening;
  }
}

void WasapiRecoveryPolicy::clear_recovery() noexcept {
  attempt_count_ = 0;
  recovery_deadline_at_ms_ = 0;
  next_attempt_at_ms_ = 0;
  recovery_active_ = false;
  recovery_pending_ = false;
  stop_requested_ = false;
}

void WasapiRecoveryPolicy::fault() noexcept {
  next_attempt_at_ms_ = 0;
  state_ = WasapiRecoveryState::Faulted;
}

void WasapiRecoveryPolicy::reset_if_stable(std::uint64_t now_ms) noexcept {
  if (state_ == WasapiRecoveryState::Running && recovery_active_ &&
      now_ms - running_since_ms_ >= kStabilityWindowMs) {
    clear_recovery();
  }
}

}  // namespace sar::platform
