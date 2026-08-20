#include "core/platform/windows_asio_vendor_host.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <atomic>
#include <new>
#include <thread>
#include <utility>

namespace sar::platform {
namespace {

std::atomic<WindowsAsioCallbackTransport*> callback_transport{nullptr};
std::atomic_uint32_t callbacks_in_flight{0};
std::atomic_flag callback_slot = ATOMIC_FLAG_INIT;

void buffer_switch(long index, ASIOBool) {
  callbacks_in_flight.fetch_add(1, std::memory_order_acquire);
  if (auto* transport = callback_transport.load(std::memory_order_acquire)) {
    (void)transport->process(static_cast<std::uint32_t>(index));
  }
  callbacks_in_flight.fetch_sub(1, std::memory_order_release);
}

ASIOTime* buffer_switch_time_info(ASIOTime* time, long index, ASIOBool direct) {
  buffer_switch(index, direct);
  return time;
}

void sample_rate_changed(ASIOSampleRate) {}
long asio_message(long, long, void*, double*) { return 0; }

ASIOCallbacks callbacks{buffer_switch, sample_rate_changed, asio_message,
                        buffer_switch_time_info};

bool asio_ok(long value) noexcept { return value == ASE_OK || value == ASE_SUCCESS; }

class IasioLifecycle final : public WindowsAsioDriverLifecycle {
 public:
  explicit IasioLifecycle(IASIO* driver) noexcept : driver_(driver) {}
  long create_buffers(ASIOBufferInfo* infos, long count, long frames,
                      ASIOCallbacks* cb) noexcept override {
    return driver_->createBuffers(infos, count, frames, cb);
  }
  long start() noexcept override { return driver_->start(); }
  long stop() noexcept override { return driver_->stop(); }
  long dispose_buffers() noexcept override { return driver_->disposeBuffers(); }
  void release() noexcept override {
    if (driver_) {
      driver_->Release();
      driver_ = nullptr;
    }
  }
 private:
  IASIO* driver_ = nullptr;
};

}  // namespace

std::unique_ptr<WindowsAsioDriverLifecycle> make_windows_asio_driver_lifecycle(
    IASIO* driver) noexcept {
  if (!driver) return {};
  return std::unique_ptr<WindowsAsioDriverLifecycle>(new (std::nothrow) IasioLifecycle(driver));
}

WindowsAsioVendorHost::WindowsAsioVendorHost(
    std::unique_ptr<WindowsAsioDriverLifecycle> driver) noexcept
    : driver_(std::move(driver)) {}

WindowsAsioVendorHost::~WindowsAsioVendorHost() { (void)teardown(); }

std::unique_ptr<WindowsAsioVendorHost> WindowsAsioVendorHost::create(
    std::unique_ptr<WindowsAsioDriverLifecycle> driver,
    WindowsAsioVendorHostConfig config, WindowsAsioVendorHostResult& result) noexcept {
  result = {};
  if (!driver || config.frames_per_block == 0 || config.channels.empty() ||
      !config.graph_process) {
    result.error = WindowsAsioVendorHostError::InvalidConfiguration;
    return {};
  }
  auto host = std::unique_ptr<WindowsAsioVendorHost>(
      new (std::nothrow) WindowsAsioVendorHost(std::move(driver)));
  if (!host) {
    result.error = WindowsAsioVendorHostError::InvalidConfiguration;
    return {};
  }
  if (callback_slot.test_and_set(std::memory_order_acq_rel)) {
    result.error = WindowsAsioVendorHostError::CallbackSlotBusy;
    return {};
  }
  std::vector<ASIOBufferInfo> infos(config.channels.size());
  for (std::size_t i = 0; i < config.channels.size(); ++i) {
    infos[i].isInput = config.channels[i].input ? ASIOTrue : ASIOFalse;
    infos[i].channelNum = config.channels[i].channel;
  }
  if (!asio_ok(host->driver_->create_buffers(infos.data(), static_cast<long>(infos.size()),
                                               static_cast<long>(config.frames_per_block),
                                               &callbacks))) {
    result.error = WindowsAsioVendorHostError::CreateBuffersFailed;
    return {};
  }
  host->buffers_created_ = true;
  WindowsAsioCallbackTransportConfig transport_config;
  transport_config.frames_per_block = config.frames_per_block;
  for (std::size_t i = 0; i < infos.size(); ++i) {
    WindowsAsioChannelBinding binding{config.channels[i].encoding,
                                      {infos[i].buffers[0], infos[i].buffers[1]}};
    (config.channels[i].input ? transport_config.inputs : transport_config.outputs)
        .push_back(binding);
  }
  auto opened = WindowsAsioCallbackTransport::create(
      std::move(transport_config), config.graph_process, config.graph_context);
  if (!opened.ok()) {
    result.error = WindowsAsioVendorHostError::TransportCreationFailed;
    return {};
  }
  host->transport_ = std::move(opened.transport);
  callback_transport.store(host->transport_.get(), std::memory_order_release);
  return host;
}

WindowsAsioVendorHostResult WindowsAsioVendorHost::start() noexcept {
  if (released_) return {WindowsAsioVendorHostError::InvalidConfiguration};
  if (running_) return {};
  if (!asio_ok(driver_->start())) return {WindowsAsioVendorHostError::StartFailed};
  running_ = true;
  return {};
}

WindowsAsioVendorHostResult WindowsAsioVendorHost::stop() noexcept {
  if (!running_) return {};
  if (!asio_ok(driver_->stop())) return {WindowsAsioVendorHostError::StopFailed};
  running_ = false;
  return {};
}

WindowsAsioVendorHostResult WindowsAsioVendorHost::teardown() noexcept {
  WindowsAsioVendorHostError first = WindowsAsioVendorHostError::None;
  if (running_) {
    if (!asio_ok(driver_->stop())) first = WindowsAsioVendorHostError::StopFailed;
    running_ = false;
  }
  callback_transport.store(nullptr, std::memory_order_release);
  while (callbacks_in_flight.load(std::memory_order_acquire) != 0)
    std::this_thread::yield();
  transport_.reset();
  if (buffers_created_) {
    if (!asio_ok(driver_->dispose_buffers()) && first == WindowsAsioVendorHostError::None)
      first = WindowsAsioVendorHostError::DisposeBuffersFailed;
    buffers_created_ = false;
  }
  if (!released_ && driver_) {
    driver_->release();
    released_ = true;
  }
  callback_slot.clear(std::memory_order_release);
  return {first};
}

}  // namespace sar::platform
