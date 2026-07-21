#pragma once

#include "core/realtime/audio_buffer.h"
#include "core/service/windows_virtual_asio_broker_client.h"
#include "third_party/asio_sdk_2.3.4/common/asiosys.h"
#include "third_party/asio_sdk_2.3.4/common/asio.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sar::driver {

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

  [[nodiscard]] bool start(std::string& error);
  void stop() noexcept;
  void disconnect() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint64_t sample_position() const noexcept;

 private:
  WindowsVirtualAsioRuntime(
      WindowsVirtualAsioRuntimeConfig config,
      std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker);

  void run() noexcept;
  void process_cycle(std::uint32_t buffer_index) noexcept;
  [[nodiscard]] bool arm_timer() noexcept;

  WindowsVirtualAsioRuntimeConfig config_;
  std::unique_ptr<service::WindowsVirtualAsioBrokerClient> broker_;
  realtime::AudioBuffer host_output_;
  realtime::AudioBuffer host_input_;
  void* stop_event_ = nullptr;
  void* timer_ = nullptr;
  std::thread worker_;
  std::atomic_bool running_ = false;
  std::atomic_uint64_t sample_position_ = 0;
  std::uint64_t sequence_ = 0;
  std::int64_t period_100ns_ = 0;
};

}  // namespace sar::driver
