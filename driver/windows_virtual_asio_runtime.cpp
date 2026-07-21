#include "driver/windows_virtual_asio_runtime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include "core/platform/windows_realtime_thread.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <utility>

namespace sar::driver {
namespace {

constexpr std::uint32_t kQueueCapacityBlocks = 8;

void write_u64(std::uint64_t value, ASIOSamples& output) noexcept {
  output.hi = static_cast<unsigned long>(value >> 32U);
  output.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void write_u64(std::uint64_t value, ASIOTimeStamp& output) noexcept {
  output.hi = static_cast<unsigned long>(value >> 32U);
  output.lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

std::uint64_t qpc_100ns() noexcept {
  LARGE_INTEGER counter{};
  LARGE_INTEGER frequency{};
  if (!QueryPerformanceCounter(&counter) ||
      !QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) {
    return GetTickCount64() * 10000ULL;
  }
  const auto whole = counter.QuadPart / frequency.QuadPart;
  const auto remainder = counter.QuadPart % frequency.QuadPart;
  return static_cast<std::uint64_t>(whole) * 10000000ULL +
         static_cast<std::uint64_t>(
             (remainder * 10000000LL) / frequency.QuadPart);
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

}  // namespace

WindowsVirtualAsioRuntime::WindowsVirtualAsioRuntime(
    WindowsVirtualAsioRuntimeConfig config,
    std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker)
    : config_(std::move(config)),
      broker_(std::move(broker)),
      host_output_(config_.input_channels, config_.frames_per_block),
      host_input_(config_.output_channels, config_.frames_per_block),
      period_100ns_(-static_cast<std::int64_t>(
          (static_cast<std::uint64_t>(config_.frames_per_block) * 10000000ULL) /
          config_.sample_rate)) {}

WindowsVirtualAsioRuntime::~WindowsVirtualAsioRuntime() {
  stop();
  disconnect();
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
                std::move(config), connected.take_client())),
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
  timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
  if (stop_event_ == nullptr || timer_ == nullptr || !arm_timer()) {
    close_handle(timer_);
    close_handle(stop_event_);
    error = "Could not create the ASIO callback scheduler.";
    return false;
  }

  sample_position_.store(0, std::memory_order_release);
  sequence_ = 0;
  running_.store(true, std::memory_order_release);
  try {
    worker_ = std::thread([this] { run(); });
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

bool WindowsVirtualAsioRuntime::arm_timer() noexcept {
  LARGE_INTEGER due{};
  due.QuadPart = period_100ns_;
  return SetWaitableTimer(static_cast<HANDLE>(timer_), &due, 0, nullptr,
                          nullptr, FALSE) != FALSE;
}

void WindowsVirtualAsioRuntime::run() noexcept {
  platform::WindowsRealtimeThreadScope realtime_scope;
  static_cast<void>(
      platform::WindowsRealtimeThreadScope::enter_current_thread(realtime_scope));
  const HANDLE waits[] = {
      static_cast<HANDLE>(stop_event_),
      static_cast<HANDLE>(timer_),
  };
  std::uint32_t buffer_index = 0;
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
    if (!arm_timer()) {
      break;
    }
  }
  running_.store(false, std::memory_order_release);
}

void WindowsVirtualAsioRuntime::process_cycle(
    std::uint32_t buffer_index) noexcept {
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

  const auto position = sample_position_.load(std::memory_order_relaxed);
  const auto timestamp_100ns = qpc_100ns();
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
  ++sequence_;
  sample_position_.fetch_add(config_.frames_per_block,
                             std::memory_order_release);
}

}  // namespace sar::driver
