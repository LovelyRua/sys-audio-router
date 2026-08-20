#include "core/platform/windows_asio_control_open.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

namespace sar::platform {
namespace {

constexpr long kMaximumPhysicalAsioChannelsPerDirection = 1024;

bool asio_ok(long value) noexcept { return value == ASE_OK || value == ASE_SUCCESS; }

bool parse_clsid(const std::string& text, CLSID& clsid) noexcept {
  if (text.empty() || text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    return false;
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) return false;
  std::wstring wide(static_cast<std::size_t>(count), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                          static_cast<int>(text.size()), wide.data(), count) != count)
    return false;
  return SUCCEEDED(CLSIDFromString(wide.c_str(), &clsid));
}

class ComAsioLifecycle final : public WindowsAsioDriverLifecycle {
 public:
  ComAsioLifecycle(IASIO* driver, bool uninitialize) noexcept
      : driver_(driver), uninitialize_(uninitialize) {}
  ~ComAsioLifecycle() override { release(); }
  long create_buffers(ASIOBufferInfo* infos, long count, long frames,
                      ASIOCallbacks* callbacks) noexcept override {
    return driver_ ? driver_->createBuffers(infos, count, frames, callbacks) : ASE_NotPresent;
  }
  long start() noexcept override { return driver_ ? driver_->start() : ASE_NotPresent; }
  long stop() noexcept override { return driver_ ? driver_->stop() : ASE_NotPresent; }
  long dispose_buffers() noexcept override {
    return driver_ ? driver_->disposeBuffers() : ASE_NotPresent;
  }
  void release() noexcept override {
    if (driver_) {
      driver_->Release();
      driver_ = nullptr;
    }
    if (uninitialize_) {
      CoUninitialize();
      uninitialize_ = false;
    }
  }
 private:
  IASIO* driver_ = nullptr;
  bool uninitialize_ = false;
};

class ComActivatedDriver final : public WindowsAsioActivatedDriver {
 public:
  ComActivatedDriver(IASIO* driver, bool uninitialize) noexcept
      : driver_(driver), uninitialize_(uninitialize) {}
  ~ComActivatedDriver() override {
    if (driver_) driver_->Release();
    if (uninitialize_) CoUninitialize();
  }
  bool initialize(void* reference) noexcept override {
    return driver_ && driver_->init(reference) == ASIOTrue;
  }
  bool can_sample_rate(double rate) noexcept override {
    return driver_ && asio_ok(driver_->canSampleRate(rate));
  }
  bool set_sample_rate(double rate) noexcept override {
    return driver_ && asio_ok(driver_->setSampleRate(rate));
  }
  bool channels(long& inputs, long& outputs) noexcept override {
    return driver_ && asio_ok(driver_->getChannels(&inputs, &outputs));
  }
  bool buffer_sizes(long& minimum, long& maximum, long& preferred,
                    long& granularity) noexcept override {
    return driver_ && asio_ok(driver_->getBufferSize(&minimum, &maximum,
                                                     &preferred, &granularity));
  }
  bool channel_info(long channel, bool input, long& sample_type,
                    std::string& name) noexcept override {
    if (!driver_) return false;
    ASIOChannelInfo info{};
    info.channel = channel;
    info.isInput = input ? ASIOTrue : ASIOFalse;
    if (!asio_ok(driver_->getChannelInfo(&info))) return false;
    sample_type = static_cast<long>(info.type);
    name = info.name;
    return true;
  }
  std::unique_ptr<WindowsAsioDriverLifecycle> take_lifecycle() noexcept override {
    if (!driver_) return {};
    auto lifecycle = std::unique_ptr<WindowsAsioDriverLifecycle>(
        new (std::nothrow) ComAsioLifecycle(driver_, uninitialize_));
    if (lifecycle) {
      driver_ = nullptr;
      uninitialize_ = false;
    }
    return lifecycle;
  }
 private:
  IASIO* driver_ = nullptr;
  bool uninitialize_ = false;
};

class ComDriverActivator final : public WindowsAsioDriverActivator {
 public:
  std::unique_ptr<WindowsAsioActivatedDriver> activate(
      const std::string& clsid_text) noexcept override {
    CLSID clsid{};
    if (!parse_clsid(clsid_text, clsid)) return {};
    const auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool owns_com = com == S_OK || com == S_FALSE;
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) return {};
    IASIO* driver = nullptr;
    const auto created = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                          clsid, reinterpret_cast<void**>(&driver));
    if (FAILED(created) || !driver) {
      if (owns_com) CoUninitialize();
      return {};
    }
    auto result = std::unique_ptr<WindowsAsioActivatedDriver>(
        new (std::nothrow) ComActivatedDriver(driver, owns_com));
    if (!result) {
      driver->Release();
      if (owns_com) CoUninitialize();
    }
    return result;
  }
};

bool sample_encoding(long type, WindowsAsioSampleEncoding& encoding) noexcept {
  switch (static_cast<ASIOSampleType>(type)) {
    case ASIOSTInt16LSB: encoding = WindowsAsioSampleEncoding::Int16Lsb; return true;
    case ASIOSTInt24LSB: encoding = WindowsAsioSampleEncoding::Int24Lsb; return true;
    case ASIOSTInt32LSB: encoding = WindowsAsioSampleEncoding::Int32Lsb; return true;
    case ASIOSTInt32LSB16: encoding = WindowsAsioSampleEncoding::Int32Lsb16; return true;
    case ASIOSTInt32LSB18: encoding = WindowsAsioSampleEncoding::Int32Lsb18; return true;
    case ASIOSTInt32LSB20: encoding = WindowsAsioSampleEncoding::Int32Lsb20; return true;
    case ASIOSTInt32LSB24: encoding = WindowsAsioSampleEncoding::Int32Lsb24; return true;
    case ASIOSTFloat32LSB: encoding = WindowsAsioSampleEncoding::Float32Lsb; return true;
    case ASIOSTFloat64LSB: encoding = WindowsAsioSampleEncoding::Float64Lsb; return true;
    default: return false;
  }
}

bool valid_block(long value, long minimum, long maximum, long granularity) noexcept {
  if (value < minimum || value > maximum) return false;
  if (granularity > 0) return (value - minimum) % granularity == 0;
  if (granularity == -1) return value > 0 && (value & (value - 1)) == 0;
  return true;
}

class DefaultNegotiator final : public WindowsAsioDriverNegotiator {
 public:
  WindowsAsioNegotiationResult negotiate(
      WindowsAsioActivatedDriver& driver,
      const WindowsAsioControlOpenRequest& request) noexcept override {
    if (!driver.initialize(request.system_reference))
      return {WindowsAsioControlOpenError::InitializationFailed, {}};
    if (!driver.can_sample_rate(request.sample_rate))
      return {WindowsAsioControlOpenError::SampleRateUnsupported, {}};
    if (!driver.set_sample_rate(request.sample_rate))
      return {WindowsAsioControlOpenError::SampleRateChangeFailed, {}};
    long inputs = 0, outputs = 0;
    if (!driver.channels(inputs, outputs) || inputs < 0 || outputs < 0 ||
        (inputs == 0 && outputs == 0))
      return {WindowsAsioControlOpenError::ChannelQueryFailed, {}};
    if (inputs > kMaximumPhysicalAsioChannelsPerDirection ||
        outputs > kMaximumPhysicalAsioChannelsPerDirection)
      return {WindowsAsioControlOpenError::DriverLimitsExceeded, {}};
    long minimum = 0, maximum = 0, preferred = 0, granularity = 0;
    if (!driver.buffer_sizes(minimum, maximum, preferred, granularity) ||
        minimum <= 0 || maximum < minimum || !valid_block(preferred, minimum, maximum, granularity))
      return {WindowsAsioControlOpenError::BufferQueryFailed, {}};
    const auto requested = request.preferred_block_frames == 0
                               ? preferred
                               : static_cast<long>(request.preferred_block_frames);
    if (!valid_block(requested, minimum, maximum, granularity))
      return {WindowsAsioControlOpenError::BufferSizeUnsupported, {}};
    WindowsAsioNegotiatedConfig config;
    config.sample_rate = request.sample_rate;
    config.frames_per_block = static_cast<std::uint32_t>(requested);
    try {
      for (const bool input : {true, false}) {
        const auto count = input ? inputs : outputs;
        auto& channels = input ? config.inputs : config.outputs;
        channels.reserve(static_cast<std::size_t>(count));
        for (long index = 0; index < count; ++index) {
          long type = 0;
          std::string name;
          if (!driver.channel_info(index, input, type, name))
            return {WindowsAsioControlOpenError::ChannelInfoFailed, {}};
          WindowsAsioSampleEncoding encoding{};
          if (!sample_encoding(type, encoding))
            return {WindowsAsioControlOpenError::SampleEncodingUnsupported, {}};
          channels.push_back({static_cast<std::uint32_t>(index), input,
                              encoding, std::move(name)});
        }
      }
    } catch (...) {
      return {WindowsAsioControlOpenError::ResourceExhausted, {}};
    }
    return {WindowsAsioControlOpenError::None, std::move(config)};
  }
};

}  // namespace

std::unique_ptr<WindowsAsioDriverActivator> make_windows_asio_driver_activator() noexcept {
  return std::unique_ptr<WindowsAsioDriverActivator>(new (std::nothrow) ComDriverActivator);
}
std::unique_ptr<WindowsAsioDriverNegotiator> make_windows_asio_driver_negotiator() noexcept {
  return std::unique_ptr<WindowsAsioDriverNegotiator>(new (std::nothrow) DefaultNegotiator);
}

WindowsAsioControlOpenResult open_windows_asio_control(
    const WindowsAsioControlOpenRequest& request, WindowsAsioDriverActivator& activator,
    WindowsAsioDriverNegotiator& negotiator) noexcept {
  if (request.driver.clsid.empty() || !std::isfinite(request.sample_rate) ||
      request.sample_rate <= 0.0 || !request.graph_process)
    return {WindowsAsioControlOpenError::InvalidRequest, {}, {}};
  auto driver = activator.activate(request.driver.clsid);
  if (!driver) return {WindowsAsioControlOpenError::ActivationFailed, {}, {}};
  auto negotiated = negotiator.negotiate(*driver, request);
  if (!negotiated.ok()) return {negotiated.error, {}, {}};
  auto lifecycle = driver->take_lifecycle();
  if (!lifecycle)
    return {WindowsAsioControlOpenError::LifecycleUnavailable, {}, {}};
  WindowsAsioVendorHostConfig host_config;
  host_config.frames_per_block = negotiated.config.frames_per_block;
  host_config.graph_process = request.graph_process;
  host_config.graph_context = request.graph_context;
  host_config.host_events = request.host_events;
  try {
    host_config.channels.reserve(negotiated.config.inputs.size() +
                                 negotiated.config.outputs.size());
    for (const auto& channel : negotiated.config.inputs)
      host_config.channels.push_back(
          {static_cast<long>(channel.index), true, channel.encoding});
    for (const auto& channel : negotiated.config.outputs)
      host_config.channels.push_back(
          {static_cast<long>(channel.index), false, channel.encoding});
  } catch (...) {
    return {WindowsAsioControlOpenError::ResourceExhausted,
            std::move(negotiated.config), {}};
  }
  WindowsAsioVendorHostResult host_result;
  auto host = WindowsAsioVendorHost::create(std::move(lifecycle), std::move(host_config), host_result);
  if (!host)
    return {WindowsAsioControlOpenError::VendorHostCreationFailed,
            std::move(negotiated.config), {}};
  return {WindowsAsioControlOpenError::None, std::move(negotiated.config), std::move(host)};
}

const char* windows_asio_control_open_error_name(WindowsAsioControlOpenError error) noexcept {
  switch (error) {
    case WindowsAsioControlOpenError::None: return "none";
    case WindowsAsioControlOpenError::InvalidRequest: return "invalid_request";
    case WindowsAsioControlOpenError::ActivationFailed: return "activation_failed";
    case WindowsAsioControlOpenError::InitializationFailed: return "initialization_failed";
    case WindowsAsioControlOpenError::SampleRateUnsupported: return "sample_rate_unsupported";
    case WindowsAsioControlOpenError::SampleRateChangeFailed: return "sample_rate_change_failed";
    case WindowsAsioControlOpenError::ChannelQueryFailed: return "channel_query_failed";
    case WindowsAsioControlOpenError::BufferQueryFailed: return "buffer_query_failed";
    case WindowsAsioControlOpenError::BufferSizeUnsupported: return "buffer_size_unsupported";
    case WindowsAsioControlOpenError::ChannelInfoFailed: return "channel_info_failed";
    case WindowsAsioControlOpenError::SampleEncodingUnsupported: return "sample_encoding_unsupported";
    case WindowsAsioControlOpenError::LifecycleUnavailable: return "lifecycle_unavailable";
    case WindowsAsioControlOpenError::VendorHostCreationFailed: return "vendor_host_creation_failed";
    case WindowsAsioControlOpenError::DriverLimitsExceeded: return "driver_limits_exceeded";
    case WindowsAsioControlOpenError::ResourceExhausted: return "resource_exhausted";
  }
  return "unknown";
}

}  // namespace sar::platform
