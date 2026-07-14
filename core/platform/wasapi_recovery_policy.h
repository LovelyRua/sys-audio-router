#pragma once

#include <cstdint>
#include <string_view>

namespace sar::platform {

enum class WasapiRecoveryState {
  Stopped,
  Opening,
  Running,
  Quiescing,
  Backoff,
  Faulted,
};

enum class WasapiFailureClass {
  Transient,
  DeviceInvalidated,
  Fatal,
  Unknown,
};

[[nodiscard]] bool wasapi_failure_is_recoverable(
    WasapiFailureClass failure_class) noexcept;
[[nodiscard]] WasapiFailureClass classify_wasapi_failure_code(
    std::string_view code) noexcept;

class WasapiRecoveryPolicy {
 public:
  static constexpr std::uint32_t kMaxAttempts = 3;
  static constexpr std::uint64_t kRecoveryDeadlineMs = 5000;
  static constexpr std::uint64_t kStabilityWindowMs = 5000;

  [[nodiscard]] WasapiRecoveryState state() const noexcept;
  [[nodiscard]] std::uint32_t attempt_count() const noexcept;
  [[nodiscard]] std::uint64_t next_attempt_at_ms() const noexcept;
  [[nodiscard]] std::uint64_t recovery_deadline_at_ms() const noexcept;

  // Event timestamps must come from one monotonic millisecond clock.
  void request_start(std::uint64_t now_ms) noexcept;
  void request_stop() noexcept;
  void on_quiesced(std::uint64_t now_ms) noexcept;
  void on_open_succeeded(std::uint64_t now_ms) noexcept;
  void on_failure(WasapiFailureClass failure_class,
                  std::uint64_t now_ms) noexcept;
  void tick(std::uint64_t now_ms) noexcept;

 private:
  void begin_recovery(std::uint64_t now_ms) noexcept;
  void schedule_recovery(std::uint64_t now_ms) noexcept;
  void clear_recovery() noexcept;
  void fault() noexcept;
  void reset_if_stable(std::uint64_t now_ms) noexcept;

  WasapiRecoveryState state_ = WasapiRecoveryState::Stopped;
  std::uint32_t attempt_count_ = 0;
  std::uint64_t recovery_deadline_at_ms_ = 0;
  std::uint64_t next_attempt_at_ms_ = 0;
  std::uint64_t running_since_ms_ = 0;
  bool recovery_active_ = false;
  bool recovery_pending_ = false;
  bool stop_requested_ = false;
};

}  // namespace sar::platform
