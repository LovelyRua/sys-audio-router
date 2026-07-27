#include "driver/windows_virtual_asio_runtime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include "core/platform/windows_realtime_thread.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace sar::driver {
namespace {

constexpr std::uint32_t kQueueCapacityBlocks = 8;
constexpr std::uint64_t kHundredNanosecondsPerSecond = 10000000ULL;

void write_u64(std::uint64_t value, ASIOSamples& output) noexcept {
  output.hi = static_cast<unsigned long>(value >> 32U);
  output.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void write_u64(std::uint64_t value, ASIOTimeStamp& output) noexcept {
  output.hi = static_cast<unsigned long>(value >> 32U);
  output.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

std::uint64_t qpc_100ns(std::uint64_t frequency) noexcept {
  LARGE_INTEGER counter{};
  if (!QueryPerformanceCounter(&counter) || frequency == 0) {
    return GetTickCount64() * 10000ULL;
  }
  const auto value = static_cast<std::uint64_t>(counter.QuadPart);
  const auto whole = value / frequency;
  const auto remainder = value % frequency;
  return whole * kHundredNanosecondsPerSecond +
         (remainder * kHundredNanosecondsPerSecond) / frequency;
}

bool qpc_period(std::uint32_t sample_rate,
                std::uint32_t frames_per_block,
                std::uint64_t frequency,
                std::uint64_t& period) noexcept {
  if (sample_rate == 0 || frames_per_block == 0 || frequency == 0 ||
      frequency > std::numeric_limits<std::uint64_t>::max() /
                      kHundredNanosecondsPerSecond ||
      frames_per_block >
          std::numeric_limits<std::uint64_t>::max() / frequency) {
    return false;
  }
  const auto numerator =
      static_cast<std::uint64_t>(frames_per_block) * frequency;
  if (numerator > std::numeric_limits<std::uint64_t>::max() -
                      sample_rate / 2U) {
    return false;
  }
  period = (numerator + sample_rate / 2U) / sample_rate;
  return period != 0;
}

bool secure_random(std::array<std::uint64_t, 3>& values) noexcept {
  return BCryptGenRandom(
             nullptr, reinterpret_cast<PUCHAR>(values.data()),
             static_cast<ULONG>(sizeof(values)),
             BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;
}

std::wstring broker_pipe_name() {
  std::array<wchar_t, 512> value{};
  const auto length = GetEnvironmentVariableW(
      L"SAR_VIRTUAL_ASIO_PIPE", value.data(),
      static_cast<DWORD>(value.size()));
  if (length > 0 && length < value.size()) {
    return {value.data(), length};
  }
  return L"sys-audio-route-control-virtual-asio";
}

std::string client_id(std::uint64_t random_value) {
  std::array<char, 64> value{};
  std::snprintf(value.data(), value.size(), "daw-%08lx-%016llx",
                static_cast<unsigned long>(GetCurrentProcessId()),
                static_cast<unsigned long long>(random_value));
  return value.data();
}

void close_handle(void*& value) noexcept {
  if (value != nullptr) {
    CloseHandle(static_cast<HANDLE>(value));
    value = nullptr;
  }
}

void* create_callback_timer() noexcept {
#ifdef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
  if (auto* timer = CreateWaitableTimerExW(
          nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
          TIMER_MODIFY_STATE | SYNCHRONIZE)) {
    return timer;
  }
#endif
  return CreateWaitableTimerW(nullptr, FALSE, nullptr);
}

}  // namespace

WindowsVirtualAsioRuntime::WindowsVirtualAsioRuntime(
    WindowsVirtualAsioRuntimeConfig config,
    std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker,
    std::uint64_t qpc_frequency,
    std::uint64_t period_qpc)
    : config_(std::move(config)),
      broker_(std::move(broker)),
      host_output_(config_.input_channels, config_.frames_per_block),
      host_input_(config_.output_channels, config_.frames_per_block),
      qpc_frequency_(qpc_frequency),
      period_qpc_(period_qpc) {}

WindowsVirtualAsioRuntime::~WindowsVirtualAsioRuntime() {
  stop();
  disconnect();
}

service::WindowsVirtualAsioBrokerFormatResult
WindowsVirtualAsioRuntime::query_engine_format(std::uint32_t timeout_ms) {
  std::array<std::uint64_t, 3> random{};
  if (!secure_random(random)) {
    return {{}, {{"virtual_asio_format_identity_failed",
                  "Could not generate the format query identity.", 0}}};
  }
  service::NamedPipeControlConfig pipe_config;
  pipe_config.pipe_name = broker_pipe_name();
  pipe_config.maximum_message_bytes =
      static_cast<std::uint32_t>(control::kVirtualAsioBrokerMaxMessageBytes);
  return service::WindowsVirtualAsioBrokerClient::query_format(
      pipe_config, random[0], timeout_ms);
}

WindowsVirtualAsioRuntimeOpenResult WindowsVirtualAsioRuntime::open(
    WindowsVirtualAsioRuntimeConfig config) {
  if (config.sample_rate == 0 || config.frames_per_block == 0 ||
      config.input_channels == 0 || config.output_channels == 0 ||
      config.callbacks == nullptr ||
      (config.use_time_info
           ? config.callbacks->bufferSwitchTimeInfo == nullptr
           : config.callbacks->bufferSwitch == nullptr)) {
    return {nullptr, "The negotiated ASIO runtime configuration is incomplete."};
  }

  LARGE_INTEGER qpc_frequency_value{};
  std::uint64_t period_qpc = 0;
  if (!QueryPerformanceFrequency(&qpc_frequency_value) ||
      qpc_frequency_value.QuadPart <= 0 ||
      !qpc_period(config.sample_rate, config.frames_per_block,
                  static_cast<std::uint64_t>(qpc_frequency_value.QuadPart),
                  period_qpc)) {
    return {nullptr, "Could not establish the ASIO callback clock."};
  }

  std::array<std::uint64_t, 3> random{};
  if (!secure_random(random)) {
    return {nullptr, "Could not generate the broker connection identity."};
  }
  service::NamedPipeControlConfig pipe_config;
  pipe_config.pipe_name = broker_pipe_name();
  pipe_config.maximum_message_bytes =
      static_cast<std::uint32_t>(control::kVirtualAsioBrokerMaxMessageBytes);
  control::VirtualAsioBrokerConnectRequest request{
      .request_id = random[0],
      .client_id = client_id(random[1]),
      .format = {
          config.sample_rate,
          config.frames_per_block,
          config.input_channels,
          config.output_channels,
      },
      .queue_capacity_blocks = kQueueCapacityBlocks,
      .client_nonce_low = random[1],
      .client_nonce_high = random[2],
  };
  auto connected = service::WindowsVirtualAsioBrokerClient::connect(
      std::move(pipe_config), std::move(request), 1000);
  if (!connected.ok()) {
    const auto& errors = connected.errors();
    return {nullptr, errors.empty() ? "The engine broker connection failed."
                                    : errors.front().message};
  }

  try {
    return {
        std::unique_ptr<WindowsVirtualAsioRuntime>(
            new WindowsVirtualAsioRuntime(
                std::move(config), connected.take_client(),
                static_cast<std::uint64_t>(qpc_frequency_value.QuadPart),
                period_qpc)),
        {},
    };
  } catch (const std::bad_alloc&) {
    return {nullptr, "Could not allocate the preconfigured ASIO runtime."};
  }
}

bool WindowsVirtualAsioRuntime::start(std::string& error) {
  if (running_.load(std::memory_order_acquire)) {
    return true;
  }
  stop_event_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  timer_ = create_callback_timer();
  LARGE_INTEGER now{};
  if (stop_event_ == nullptr || timer_ == nullptr ||
      !QueryPerformanceCounter(&now)) {
    close_handle(timer_);
    close_handle(stop_event_);
    error = "Could not create the ASIO callback scheduler.";
    return false;
  }
  const auto now_qpc = static_cast<std::uint64_t>(now.QuadPart);
  if (now_qpc > std::numeric_limits<std::uint64_t>::max() - period_qpc_ ||
      !arm_timer(now_qpc + period_qpc_, now_qpc)) {
    close_handle(timer_);
    close_handle(stop_event_);
    error = "Could not arm the ASIO callback scheduler.";
    return false;
  }
  const auto first_deadline_qpc = now_qpc + period_qpc_;

  sample_position_.store(0, std::memory_order_release);
  sequence_ = 0;
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread(
        [this, first_deadline_qpc] { run(first_deadline_qpc); });
  } catch (...) {
    running_.store(false, std::memory_order_release);
    close_handle(timer_);
    close_handle(stop_event_);
    error = "Could not create the ASIO callback thread.";
    return false;
  }
  return true;
}

void WindowsVirtualAsioRuntime::stop() noexcept {
  if (stop_event_ != nullptr) {
    SetEvent(static_cast<HANDLE>(stop_event_));
  }
  if (worker_.joinable()) {
    worker_.join();
  }
  running_.store(false, std::memory_order_release);
  close_handle(timer_);
  close_handle(stop_event_);
}

void WindowsVirtualAsioRuntime::disconnect() noexcept {
  if (broker_ != nullptr) {
    static_cast<void>(broker_->disconnect(250));
    broker_.reset();
  }
}

bool WindowsVirtualAsioRuntime::running() const noexcept {
  return running_.load(std::memory_order_acquire);
}

std::uint64_t WindowsVirtualAsioRuntime::sample_position() const noexcept {
  return sample_position_.load(std::memory_order_acquire);
}

bool WindowsVirtualAsioRuntime::arm_timer(std::uint64_t deadline_qpc,
                                         std::uint64_t now_qpc) noexcept {
  const auto remaining_qpc =
      deadline_qpc > now_qpc ? deadline_qpc - now_qpc : 0;
  const auto whole_seconds = remaining_qpc / qpc_frequency_;
  const auto remainder_qpc = remaining_qpc % qpc_frequency_;
  if (whole_seconds >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) /
          kHundredNanosecondsPerSecond) {
    return false;
  }
  const auto fractional_100ns =
      (remainder_qpc * kHundredNanosecondsPerSecond + qpc_frequency_ - 1) /
      qpc_frequency_;
  const auto whole_100ns = whole_seconds * kHundredNanosecondsPerSecond;
  if (fractional_100ns >
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
          whole_100ns) {
    return false;
  }
  const auto delay_100ns = whole_100ns + fractional_100ns;
  LARGE_INTEGER due{};
  due.QuadPart = -static_cast<std::int64_t>(std::max<std::uint64_t>(
      delay_100ns, 1));
  return SetWaitableTimer(static_cast<HANDLE>(timer_), &due, 0, nullptr,
                          nullptr, FALSE) != FALSE;
}

void WindowsVirtualAsioRuntime::run(std::uint64_t first_deadline_qpc) noexcept {
  platform::WindowsRealtimeThreadScope realtime_scope;
  static_cast<void>(
      platform::WindowsRealtimeThreadScope::enter_current_thread(realtime_scope));
  const HANDLE waits[] = {
      static_cast<HANDLE>(stop_event_),
      static_cast<HANDLE>(timer_),
  };
  std::uint32_t buffer_index = 0;
  auto deadline_qpc = first_deadline_qpc;
  while (running_.load(std::memory_order_acquire)) {
    const auto wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
    if (wait == WAIT_OBJECT_0) {
      break;
    }
    if (wait != WAIT_OBJECT_0 + 1) {
      break;
    }
    process_cycle(buffer_index);
    buffer_index ^= 1U;
    LARGE_INTEGER now{};
    if (!QueryPerformanceCounter(&now)) {
      break;
    }
    const auto schedule = detail::advance_windows_virtual_asio_deadline(
        deadline_qpc, period_qpc_, static_cast<std::uint64_t>(now.QuadPart));
    deadline_qpc = schedule.deadline_qpc;
    if (!arm_timer(deadline_qpc,
                   static_cast<std::uint64_t>(now.QuadPart))) {
      break;
    }
  }
  running_.store(false, std::memory_order_release);
}

void WindowsVirtualAsioRuntime::process_cycle(
    std::uint32_t buffer_index) noexcept {
  const auto position = sample_position_.load(std::memory_order_relaxed);
  const auto timestamp_100ns = qpc_100ns(qpc_frequency_);

  // Inputs must be visible to the host in the half named by this callback.
  host_input_.clear();
  platform::VirtualAsioSharedBlockMetadata received{};
  if (broker_ != nullptr) {
    static_cast<void>(broker_->pop_output(host_input_, received));
  }
  for (const auto& binding : config_.bindings) {
    if (!binding.host_input || binding.channel >= host_input_.channels() ||
        binding.halves[buffer_index] == nullptr) {
      continue;
    }
    std::memcpy(binding.halves[buffer_index],
                host_input_.channel(binding.channel).data(),
                config_.frames_per_block * sizeof(float));
  }

  if (config_.use_time_info &&
      config_.callbacks->bufferSwitchTimeInfo != nullptr) {
    ASIOTime time{};
    time.timeInfo.speed = 1.0;
    time.timeInfo.sampleRate = config_.sample_rate;
    time.timeInfo.flags = kSystemTimeValid | kSamplePositionValid |
                          kSampleRateValid | kSpeedValid;
    write_u64(position, time.timeInfo.samplePosition);
    write_u64(timestamp_100ns * 100ULL, time.timeInfo.systemTime);
    config_.callbacks->bufferSwitchTimeInfo(
        &time, static_cast<long>(buffer_index), ASIOFalse);
  } else {
    config_.callbacks->bufferSwitch(static_cast<long>(buffer_index), ASIOFalse);
  }

  // The callback owns the selected output half until it returns. Read it only
  // after the host has produced this cycle's samples.
  host_output_.clear();
  for (const auto& binding : config_.bindings) {
    if (binding.host_input || binding.channel >= host_output_.channels() ||
        binding.halves[buffer_index] == nullptr) {
      continue;
    }
    std::memcpy(host_output_.channel(binding.channel).data(),
                binding.halves[buffer_index],
                config_.frames_per_block * sizeof(float));
  }
  const platform::VirtualAsioSharedBlockMetadata sent{
      .sequence = sequence_,
      .sample_position = position,
      .qpc_position_100ns = timestamp_100ns,
      .flags = 0,
  };
  if (broker_ != nullptr &&
      broker_->push_input(host_output_, sent) ==
          platform::VirtualAsioSharedQueueStatus::Completed) {
    static_cast<void>(broker_->signal_input());
  }
  ++sequence_;
  sample_position_.fetch_add(config_.frames_per_block,
                             std::memory_order_release);
}

}  // namespace sar::driver
