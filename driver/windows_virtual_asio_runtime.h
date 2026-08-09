#pragma once

#include "core/realtime/audio_buffer.h"
#include "core/service/windows_virtual_asio_broker_client.h"
#include "third_party/asio_sdk_2.3.4/common/asiosys.h"
#include "third_party/asio_sdk_2.3.4/common/asio.h"

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sar::driver {

namespace detail {

struct WindowsVirtualAsioPeriodicDeadline {
  std::uint64_t deadline_qpc = 0;
  std::uint64_t skipped_periods = 0;
};

[[nodiscard]] constexpr WindowsVirtualAsioPeriodicDeadline
advance_windows_virtual_asio_deadline(std::uint64_t deadline_qpc,
                                      std::uint64_t period_qpc,
                                      std::uint64_t now_qpc) noexcept {
  if (period_qpc == 0) {
    return {deadline_qpc, 0};
  }

  constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
  if (deadline_qpc > maximum - period_qpc) {
    return {maximum, 0};
  }
  auto next = deadline_qpc + period_qpc;
  if (next >= now_qpc) {
    return {next, 0};
  }

  const auto skipped = (now_qpc - next) / period_qpc + 1;
  if (skipped > (maximum - next) / period_qpc) {
    return {maximum, skipped};
  }
  return {next + skipped * period_qpc, skipped};
}

class WindowsVirtualAsioRationalClock {
 public:
  constexpr WindowsVirtualAsioRationalClock(std::uint64_t whole_period_qpc,
                                            std::uint64_t remainder_qpc,
                                            std::uint64_t denominator) noexcept
      : whole_period_qpc_(whole_period_qpc),
        remainder_qpc_(remainder_qpc),
        denominator_(denominator) {}

  [[nodiscard]] constexpr WindowsVirtualAsioPeriodicDeadline reset(
      std::uint64_t anchor_qpc) noexcept {
    deadline_qpc_ = anchor_qpc;
    remainder_accumulator_ = 0;
    return advance_one();
  }

  [[nodiscard]] constexpr WindowsVirtualAsioPeriodicDeadline advance(
      std::uint64_t now_qpc) noexcept {
    auto next = advance_one();
    if (next.deadline_qpc >= now_qpc) {
      return next;
    }

    // A late host callback is an xrun boundary. Re-anchor instead of issuing a
    // catch-up burst, while retaining exact average cadence after recovery.
    next = reset(now_qpc);
    next.skipped_periods = 1;
    return next;
  }

 private:
  [[nodiscard]] constexpr WindowsVirtualAsioPeriodicDeadline advance_one()
      noexcept {
    if (denominator_ == 0 ||
        (whole_period_qpc_ == 0 && remainder_qpc_ == 0)) {
      return {deadline_qpc_, 0};
    }

    remainder_accumulator_ += remainder_qpc_;
    auto increment = whole_period_qpc_;
    if (remainder_accumulator_ >= denominator_) {
      increment += remainder_accumulator_ / denominator_;
      remainder_accumulator_ %= denominator_;
    }
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    deadline_qpc_ = deadline_qpc_ > maximum - increment
                        ? maximum
                        : deadline_qpc_ + increment;
    return {deadline_qpc_, 0};
  }

  std::uint64_t whole_period_qpc_ = 0;
  std::uint64_t remainder_qpc_ = 0;
  std::uint64_t denominator_ = 0;
  std::uint64_t deadline_qpc_ = 0;
  std::uint64_t remainder_accumulator_ = 0;
};

}  // namespace detail

struct WindowsVirtualAsioBufferBinding {
  bool host_input = false;
  std::uint32_t channel = 0;
  float* halves[2] = {};
};

struct WindowsVirtualAsioRuntimeConfig {
  std::uint32_t sample_rate = 0;
  std::uint32_t frames_per_block = 0;
  std::uint32_t input_channels = 0;
  std::uint32_t output_channels = 0;
  std::vector<WindowsVirtualAsioBufferBinding> bindings;
  ASIOCallbacks* callbacks = nullptr;
  bool use_time_info = false;
};

struct WindowsVirtualAsioRuntimeOpenResult {
  std::unique_ptr<class WindowsVirtualAsioRuntime> runtime;
  std::string error;

  [[nodiscard]] bool ok() const noexcept { return runtime != nullptr; }
};

class WindowsVirtualAsioRuntime {
 public:
  WindowsVirtualAsioRuntime(const WindowsVirtualAsioRuntime&) = delete;
  WindowsVirtualAsioRuntime& operator=(const WindowsVirtualAsioRuntime&) = delete;
  ~WindowsVirtualAsioRuntime();

  [[nodiscard]] static WindowsVirtualAsioRuntimeOpenResult open(
      WindowsVirtualAsioRuntimeConfig config);
  [[nodiscard]] static service::WindowsVirtualAsioBrokerFormatResult
  query_engine_format(std::uint32_t timeout_ms = 250);

  [[nodiscard]] bool start(std::string& error);
  void stop() noexcept;
  void disconnect() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint64_t sample_position() const noexcept;

 private:
  WindowsVirtualAsioRuntime(
      WindowsVirtualAsioRuntimeConfig config,
      std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker,
      void* broker_process_handle,
      std::uint64_t qpc_frequency,
      std::uint64_t whole_period_qpc,
      std::uint64_t period_remainder_qpc);

  void run() noexcept;
  [[nodiscard]] bool process_cycle(std::uint32_t buffer_index) noexcept;
  [[nodiscard]] bool broker_process_exited() const noexcept;
  [[nodiscard]] bool arm_timer(std::uint64_t deadline_qpc,
                               std::uint64_t now_qpc) noexcept;

  WindowsVirtualAsioRuntimeConfig config_;
  std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker_;
  void* broker_process_handle_ = nullptr;
  realtime::AudioBuffer host_output_;
  realtime::AudioBuffer host_input_;
  realtime::AudioBuffer output_scratch_;
  void* stop_event_ = nullptr;
  void* timer_ = nullptr;
  std::thread worker_;
  std::atomic_bool running_ = false;
  std::atomic_uint64_t sample_position_ = 0;
  std::uint64_t sequence_ = 0;
  std::uint64_t qpc_frequency_ = 0;
  detail::WindowsVirtualAsioRationalClock callback_clock_;
};

}  // namespace sar::driver
