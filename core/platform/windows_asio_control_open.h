#pragma once

#include "core/platform/windows_asio_driver_probe.h"
#include "core/platform/windows_asio_vendor_host.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

enum class WindowsAsioControlOpenError : std::uint8_t {
  None,
  InvalidRequest,
  ActivationFailed,
  InitializationFailed,
  SampleRateUnsupported,
  SampleRateChangeFailed,
  ChannelQueryFailed,
  BufferQueryFailed,
  BufferSizeUnsupported,
  ChannelInfoFailed,
  SampleEncodingUnsupported,
  LifecycleUnavailable,
  VendorHostCreationFailed,
};

struct WindowsAsioControlOpenRequest {
  WindowsAsioDriverProbe driver;
  double sample_rate = 0.0;
  std::uint32_t preferred_block_frames = 0;
  WindowsAsioGraphProcess graph_process = nullptr;
  void* graph_context = nullptr;
  void* system_reference = nullptr;
};

struct WindowsAsioNegotiatedChannel {
  std::uint32_t index = 0;
  bool input = false;
  WindowsAsioSampleEncoding encoding = WindowsAsioSampleEncoding::Float32Lsb;
  std::string name;
};

struct WindowsAsioNegotiatedConfig {
  double sample_rate = 0.0;
  std::uint32_t frames_per_block = 0;
  std::vector<WindowsAsioNegotiatedChannel> inputs;
  std::vector<WindowsAsioNegotiatedChannel> outputs;
};

class WindowsAsioActivatedDriver {
 public:
  virtual ~WindowsAsioActivatedDriver() = default;
  [[nodiscard]] virtual bool initialize(void* system_reference) noexcept = 0;
  [[nodiscard]] virtual bool can_sample_rate(double sample_rate) noexcept = 0;
  [[nodiscard]] virtual bool set_sample_rate(double sample_rate) noexcept = 0;
  [[nodiscard]] virtual bool channels(long& inputs, long& outputs) noexcept = 0;
  [[nodiscard]] virtual bool buffer_sizes(long& minimum, long& maximum,
                                          long& preferred,
                                          long& granularity) noexcept = 0;
  [[nodiscard]] virtual bool channel_info(long channel, bool input,
                                          long& sample_type,
                                          std::string& name) noexcept = 0;
  [[nodiscard]] virtual std::unique_ptr<WindowsAsioDriverLifecycle>
  take_lifecycle() noexcept = 0;
};

class WindowsAsioDriverActivator {
 public:
  virtual ~WindowsAsioDriverActivator() = default;
  [[nodiscard]] virtual std::unique_ptr<WindowsAsioActivatedDriver> activate(
      const std::string& clsid) noexcept = 0;
};

struct WindowsAsioNegotiationResult {
  WindowsAsioControlOpenError error = WindowsAsioControlOpenError::None;
  WindowsAsioNegotiatedConfig config;
  [[nodiscard]] bool ok() const noexcept {
    return error == WindowsAsioControlOpenError::None;
  }
};

class WindowsAsioDriverNegotiator {
 public:
  virtual ~WindowsAsioDriverNegotiator() = default;
  [[nodiscard]] virtual WindowsAsioNegotiationResult negotiate(
      WindowsAsioActivatedDriver& driver,
      const WindowsAsioControlOpenRequest& request) noexcept = 0;
};

struct WindowsAsioControlOpenResult {
  WindowsAsioControlOpenError error = WindowsAsioControlOpenError::None;
  WindowsAsioNegotiatedConfig config;
  std::unique_ptr<WindowsAsioVendorHost> host;
  [[nodiscard]] bool ok() const noexcept {
    return error == WindowsAsioControlOpenError::None && host != nullptr;
  }
};

[[nodiscard]] std::unique_ptr<WindowsAsioDriverActivator>
make_windows_asio_driver_activator() noexcept;
[[nodiscard]] std::unique_ptr<WindowsAsioDriverNegotiator>
make_windows_asio_driver_negotiator() noexcept;

[[nodiscard]] WindowsAsioControlOpenResult open_windows_asio_control(
    const WindowsAsioControlOpenRequest& request,
    WindowsAsioDriverActivator& activator,
    WindowsAsioDriverNegotiator& negotiator) noexcept;

[[nodiscard]] const char* windows_asio_control_open_error_name(
    WindowsAsioControlOpenError error) noexcept;

}  // namespace sar::platform
