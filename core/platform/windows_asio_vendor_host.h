#pragma once

#include "core/platform/windows_asio_callback_transport.h"

#include <cstdint>
#include <memory>
#include <vector>

struct IASIO;
struct ASIOBufferInfo;
struct ASIOCallbacks;

namespace sar::platform {

class WindowsAsioDriverLifecycle {
 public:
  virtual ~WindowsAsioDriverLifecycle() = default;
  virtual long create_buffers(ASIOBufferInfo*, long, long, ASIOCallbacks*) noexcept = 0;
  virtual long start() noexcept = 0;
  virtual long stop() noexcept = 0;
  virtual long dispose_buffers() noexcept = 0;
  virtual void release() noexcept = 0;
};

[[nodiscard]] std::unique_ptr<WindowsAsioDriverLifecycle>
make_windows_asio_driver_lifecycle(IASIO* driver) noexcept;

enum class WindowsAsioVendorHostError : std::uint8_t {
  None,
  InvalidConfiguration,
  CallbackSlotBusy,
  CreateBuffersFailed,
  TransportCreationFailed,
  StartFailed,
  StopFailed,
  DisposeBuffersFailed,
  ResourceExhausted,
};

struct WindowsAsioVendorChannel {
  long channel = 0;
  bool input = false;
  WindowsAsioSampleEncoding encoding = WindowsAsioSampleEncoding::Float32Lsb;
};

struct WindowsAsioVendorHostConfig {
  std::uint32_t frames_per_block = 0;
  std::vector<WindowsAsioVendorChannel> channels;
  WindowsAsioGraphProcess graph_process = nullptr;
  void* graph_context = nullptr;
};

struct WindowsAsioVendorHostResult {
  WindowsAsioVendorHostError error = WindowsAsioVendorHostError::None;
  [[nodiscard]] bool ok() const noexcept { return error == WindowsAsioVendorHostError::None; }
};

class WindowsAsioVendorHost {
 public:
  WindowsAsioVendorHost(const WindowsAsioVendorHost&) = delete;
  WindowsAsioVendorHost& operator=(const WindowsAsioVendorHost&) = delete;
  ~WindowsAsioVendorHost();

  [[nodiscard]] static std::unique_ptr<WindowsAsioVendorHost> create(
      std::unique_ptr<WindowsAsioDriverLifecycle>, WindowsAsioVendorHostConfig,
      WindowsAsioVendorHostResult&) noexcept;
  [[nodiscard]] WindowsAsioVendorHostResult start() noexcept;
  [[nodiscard]] WindowsAsioVendorHostResult stop() noexcept;
  [[nodiscard]] WindowsAsioVendorHostResult teardown() noexcept;
  [[nodiscard]] bool running() const noexcept { return running_; }

 private:
  explicit WindowsAsioVendorHost(std::unique_ptr<WindowsAsioDriverLifecycle>) noexcept;
  std::unique_ptr<WindowsAsioDriverLifecycle> driver_;
  std::unique_ptr<WindowsAsioCallbackTransport> transport_;
  bool buffers_created_ = false;
  bool running_ = false;
  bool released_ = false;
};

}  // namespace sar::platform
