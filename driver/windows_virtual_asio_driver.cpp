#include "driver/windows_virtual_asio_com.h"
#include "driver/windows_virtual_asio_runtime.h"

#include <Unknwn.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

using sar::driver::WindowsVirtualAsioRuntime;
using sar::driver::WindowsVirtualAsioRuntimeConfig;

std::atomic_ulong module_objects = 0;
std::atomic_ulong server_locks = 0;

constexpr long kInputChannels = 2;
constexpr long kOutputChannels = 2;
constexpr long kMinimumBufferFrames = 64;
constexpr long kMaximumBufferFrames = 2048;
constexpr long kPreferredBufferFrames = 256;
constexpr double kDefaultSampleRate = 48000.0;

bool supported_sample_rate(ASIOSampleRate sample_rate) noexcept {
  constexpr double rates[] = {44100.0, 48000.0, 88200.0, 96000.0,
                              176400.0, 192000.0};
  for (const auto rate : rates) {
    if (std::abs(sample_rate - rate) < 0.5) {
      return true;
    }
  }
  return false;
}

bool supported_buffer_size(long frames) noexcept {
  if (frames < kMinimumBufferFrames || frames > kMaximumBufferFrames) {
    return false;
  }
  return (frames & (frames - 1)) == 0;
}

void write_u64(std::uint64_t value, ASIOSamples* output) noexcept {
  output->hi = static_cast<unsigned long>(value >> 32U);
  output->lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void write_u64(std::uint64_t value, ASIOTimeStamp* output) noexcept {
  output->hi = static_cast<unsigned long>(value >> 32U);
  output->lo = static_cast<unsigned long>(value & 0xffffffffULL);
}

void copy_asio_string(char* destination,
                      std::size_t capacity,
                      const char* source) noexcept {
  if (destination == nullptr || capacity == 0) {
    return;
  }
  std::strncpy(destination, source, capacity - 1);
  destination[capacity - 1] = '\0';
}

class VirtualAsioDriver final : public IASIO {
 public:
  VirtualAsioDriver() noexcept { module_objects.fetch_add(1); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    // ASIO hosts conventionally request the driver's CLSID as its interface
    // identifier because IASIO does not define a separate COM IID.
    if (!IsEqualIID(iid, IID_IUnknown) &&
        !IsEqualIID(iid, sar::driver::kWindowsVirtualAsioClsid)) {
      return E_NOINTERFACE;
    }
    *object = static_cast<IASIO*>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return references_.fetch_add(1) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = references_.fetch_sub(1) - 1;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  ASIOBool init(void* system_handle) override {
    std::scoped_lock lock(control_mutex_);
    system_handle_ = system_handle;
    initialized_ = true;
    const auto format = WindowsVirtualAsioRuntime::query_engine_format();
    if (format.ok() &&
        format.format.input_channels == kOutputChannels &&
        format.format.output_channels == kInputChannels &&
        format.format.sample_rate != 0 &&
        format.format.frames_per_block <=
            static_cast<std::uint32_t>(std::numeric_limits<long>::max()) &&
        supported_buffer_size(
            static_cast<long>(format.format.frames_per_block))) {
      sample_rate_ = static_cast<ASIOSampleRate>(format.format.sample_rate);
      preferred_buffer_frames_ =
          static_cast<long>(format.format.frames_per_block);
      format_discovered_ = true;
      last_error_.clear();
    } else if (!format.errors.empty()) {
      last_error_ = format.errors.front().message;
    } else {
      last_error_ = "The engine published an incompatible ASIO format.";
    }
    return ASIOTrue;
  }

  void getDriverName(char* name) override {
    copy_asio_string(name, 32, "System Audio Route");
  }

  long getDriverVersion() override {
    return 1;
  }

  void getErrorMessage(char* message) override {
    std::scoped_lock lock(control_mutex_);
    copy_asio_string(message, 124,
                     last_error_.empty() ? "No error" : last_error_.c_str());
  }

  ASIOError start() override {
    std::scoped_lock lock(control_mutex_);
    if (!initialized_ || !buffers_created_ || runtime_ == nullptr) {
      return fail(ASE_InvalidMode,
                  "The driver must be initialized and buffers created before start.");
    }
    std::string error;
    if (!runtime_->start(error)) {
      return fail(ASE_HWMalfunction, error.c_str());
    }
    running_.store(true, std::memory_order_release);
    last_error_.clear();
    return ASE_OK;
  }

  ASIOError stop() override {
    WindowsVirtualAsioRuntime* runtime = nullptr;
    {
      std::scoped_lock lock(control_mutex_);
      runtime = runtime_.get();
    }
    if (runtime != nullptr) {
      runtime->stop();
    }
    running_.store(false, std::memory_order_release);
    return ASE_OK;
  }

  ASIOError getChannels(long* input_channels,
                        long* output_channels) override {
    if (input_channels == nullptr || output_channels == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Channel count output pointers must not be null.");
    }
    *input_channels = kInputChannels;
    *output_channels = kOutputChannels;
    return ASE_OK;
  }

  ASIOError getLatencies(long* input_latency, long* output_latency) override {
    if (input_latency == nullptr || output_latency == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Latency output pointers must not be null.");
    }
    std::scoped_lock lock(control_mutex_);
    const auto frames = buffer_frames_ == 0 ? preferred_buffer_frames_
                                            : buffer_frames_;
    *input_latency = frames;
    *output_latency = frames;
    return ASE_OK;
  }

  ASIOError getBufferSize(long* minimum,
                          long* maximum,
                          long* preferred,
                          long* granularity) override {
    if (minimum == nullptr || maximum == nullptr || preferred == nullptr ||
        granularity == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Buffer size output pointers must not be null.");
    }
    if (format_discovered_) {
      *minimum = preferred_buffer_frames_;
      *maximum = preferred_buffer_frames_;
      *preferred = preferred_buffer_frames_;
      *granularity = 0;
    } else {
      *minimum = kMinimumBufferFrames;
      *maximum = kMaximumBufferFrames;
      *preferred = preferred_buffer_frames_;
      *granularity = -1;
    }
    return ASE_OK;
  }

  ASIOError canSampleRate(ASIOSampleRate sample_rate) override {
    std::scoped_lock lock(control_mutex_);
    if (format_discovered_) {
      return sample_rate == sample_rate_ ? ASE_OK : ASE_NoClock;
    }
    return supported_sample_rate(sample_rate) ? ASE_OK : ASE_NoClock;
  }

  ASIOError getSampleRate(ASIOSampleRate* sample_rate) override {
    if (sample_rate == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Sample rate output pointer must not be null.");
    }
    std::scoped_lock lock(control_mutex_);
    *sample_rate = sample_rate_;
    return ASE_OK;
  }

  ASIOError setSampleRate(ASIOSampleRate sample_rate) override {
    std::scoped_lock lock(control_mutex_);
    if (!supported_sample_rate(sample_rate)) {
      return fail(ASE_NoClock, "The requested sample rate is not supported.");
    }
    if (format_discovered_ && sample_rate != sample_rate_) {
      return fail(ASE_NoClock,
                  "The requested sample rate does not match the engine.");
    }
    if (running_.load(std::memory_order_acquire)) {
      return fail(ASE_InvalidMode,
                  "The sample rate cannot be changed while streaming.");
    }
    sample_rate_ = sample_rate;
    last_error_.clear();
    return ASE_OK;
  }

  ASIOError getClockSources(ASIOClockSource* clocks,
                            long* source_count) override {
    if (source_count == nullptr || *source_count < 1 || clocks == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "One clock source entry is required.");
    }
    clocks[0] = {};
    clocks[0].index = 0;
    clocks[0].associatedChannel = -1;
    clocks[0].associatedGroup = -1;
    clocks[0].isCurrentSource = ASIOTrue;
    copy_asio_string(clocks[0].name, sizeof(clocks[0].name), "Internal");
    *source_count = 1;
    return ASE_OK;
  }

  ASIOError setClockSource(long reference) override {
    return reference == 0
               ? ASE_OK
               : fail_thread_safe(ASE_InvalidParameter,
                                  "Only the internal clock source is supported.");
  }

  ASIOError getSamplePosition(ASIOSamples* sample_position,
                              ASIOTimeStamp* timestamp) override {
    if (sample_position == nullptr || timestamp == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Sample position output pointers must not be null.");
    }
    auto* runtime = runtime_.get();
    write_u64(runtime == nullptr ? 0 : runtime->sample_position(),
              sample_position);
    write_u64(GetTickCount64() * 1000000ULL, timestamp);
    return running_.load(std::memory_order_acquire) ? ASE_OK
                                                    : ASE_SPNotAdvancing;
  }

  ASIOError getChannelInfo(ASIOChannelInfo* info) override {
    if (info == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Channel information pointer must not be null.");
    }
    const auto channel_count = info->isInput == ASIOTrue ? kInputChannels
                                                         : kOutputChannels;
    if (info->channel < 0 || info->channel >= channel_count) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "The requested channel does not exist.");
    }
    std::scoped_lock lock(control_mutex_);
    info->isActive = channel_active(info->isInput, info->channel) ? ASIOTrue
                                                                  : ASIOFalse;
    info->channelGroup = 0;
    info->type = ASIOSTFloat32LSB;
    const std::string name =
        std::string(info->isInput == ASIOTrue ? "Input " : "Output ") +
        std::to_string(info->channel + 1);
    copy_asio_string(info->name, sizeof(info->name), name.c_str());
    return ASE_OK;
  }

  ASIOError createBuffers(ASIOBufferInfo* buffer_infos,
                          long channel_count,
                          long buffer_frames,
                          ASIOCallbacks* callbacks) override {
    if (buffer_infos == nullptr || callbacks == nullptr || channel_count <= 0) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Buffer descriptors and callbacks are required.");
    }
    if (channel_count > kInputChannels + kOutputChannels) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "The requested active channel count is too large.");
    }
    if (!supported_buffer_size(buffer_frames)) {
      return fail_thread_safe(ASE_InvalidMode,
                              "The requested buffer size is not supported.");
    }

    std::scoped_lock lock(control_mutex_);
    if (!initialized_ || running_.load(std::memory_order_acquire) ||
        buffers_created_) {
      return fail(ASE_InvalidMode,
                  "Buffers require an initialized, stopped, unprepared driver.");
    }
    if (format_discovered_ && buffer_frames != preferred_buffer_frames_) {
      return fail(ASE_InvalidMode,
                  "The requested buffer size does not match the engine.");
    }

    try {
      buffers_.assign(static_cast<std::size_t>(channel_count) * 2U, {});
      active_channels_.clear();
      active_channels_.reserve(static_cast<std::size_t>(channel_count));
      for (long index = 0; index < channel_count; ++index) {
        const auto& descriptor = buffer_infos[index];
        const auto available = descriptor.isInput == ASIOTrue
                                   ? kInputChannels
                                   : kOutputChannels;
        if (descriptor.channelNum < 0 || descriptor.channelNum >= available ||
            channel_active(descriptor.isInput, descriptor.channelNum)) {
          reset_buffers();
          return fail(ASE_InvalidParameter,
                      "A buffer descriptor contains an invalid or duplicate channel.");
        }
        active_channels_.push_back(
            {descriptor.isInput == ASIOTrue, descriptor.channelNum});
        for (std::size_t half = 0; half < 2; ++half) {
          auto& storage = buffers_[static_cast<std::size_t>(index) * 2U + half];
          storage.assign(static_cast<std::size_t>(buffer_frames), 0.0F);
          buffer_infos[index].buffers[half] = storage.data();
        }
      }
    } catch (const std::bad_alloc&) {
      reset_buffers();
      return fail(ASE_NoMemory, "ASIO buffer allocation failed.");
    }

    callbacks_ = callbacks;
    buffer_frames_ = buffer_frames;
    const bool use_time_info =
        callbacks->asioMessage != nullptr &&
        callbacks->bufferSwitchTimeInfo != nullptr &&
        callbacks->asioMessage(kAsioSupportsTimeInfo, 0, nullptr, nullptr) == 1;
    WindowsVirtualAsioRuntimeConfig runtime_config{
        .sample_rate = static_cast<std::uint32_t>(sample_rate_),
        .frames_per_block = static_cast<std::uint32_t>(buffer_frames),
        .input_channels = static_cast<std::uint32_t>(kOutputChannels),
        .output_channels = static_cast<std::uint32_t>(kInputChannels),
        .callbacks = callbacks,
        .use_time_info = use_time_info,
    };
    runtime_config.bindings.reserve(active_channels_.size());
    for (std::size_t index = 0; index < active_channels_.size(); ++index) {
      const auto& channel = active_channels_[index];
      runtime_config.bindings.push_back({
          .host_input = channel.input,
          .channel = static_cast<std::uint32_t>(channel.index),
          .halves = {
              buffers_[index * 2U].data(),
              buffers_[index * 2U + 1U].data(),
          },
      });
    }
    auto opened = WindowsVirtualAsioRuntime::open(std::move(runtime_config));
    if (!opened.ok()) {
      reset_buffers();
      return fail(ASE_NotPresent, opened.error.c_str());
    }
    runtime_ = std::move(opened.runtime);
    buffers_created_ = true;
    last_error_.clear();
    return ASE_OK;
  }

  ASIOError disposeBuffers() override {
    std::scoped_lock lock(control_mutex_);
    if (running_.load(std::memory_order_acquire)) {
      return fail(ASE_InvalidMode,
                  "Streaming must be stopped before buffers are disposed.");
    }
    if (!buffers_created_) {
      return ASE_InvalidMode;
    }
    reset_buffers();
    return ASE_OK;
  }

  ASIOError controlPanel() override {
    return ASE_NotPresent;
  }

  ASIOError future(long selector, void*) override {
    switch (selector) {
      case kAsioCanTimeInfo:
        return ASE_SUCCESS;
      case kAsioCanTimeCode:
      case kAsioCanReportOverload:
        return ASE_NotPresent;
      default:
        return ASE_InvalidParameter;
    }
  }

  ASIOError outputReady() override {
    return ASE_NotPresent;
  }

 private:
  struct ActiveChannel {
    bool input = false;
    long index = 0;
  };

  ~VirtualAsioDriver() {
    if (runtime_ != nullptr) {
      runtime_->stop();
      runtime_->disconnect();
    }
    module_objects.fetch_sub(1);
  }

  ASIOError fail(ASIOError error, const char* message) {
    last_error_ = message;
    return error;
  }

  ASIOError fail_thread_safe(ASIOError error, const char* message) {
    std::scoped_lock lock(control_mutex_);
    return fail(error, message);
  }

  bool channel_active(ASIOBool input, long index) const noexcept {
    for (const auto& channel : active_channels_) {
      if (channel.input == (input == ASIOTrue) && channel.index == index) {
        return true;
      }
    }
    return false;
  }

  void reset_buffers() noexcept {
    if (runtime_ != nullptr) {
      runtime_->disconnect();
      runtime_.reset();
    }
    buffers_.clear();
    active_channels_.clear();
    callbacks_ = nullptr;
    buffer_frames_ = 0;
    buffers_created_ = false;
  }

  std::atomic_ulong references_ = 1;
  std::mutex control_mutex_;
  void* system_handle_ = nullptr;
  ASIOCallbacks* callbacks_ = nullptr;
  std::vector<std::vector<float>> buffers_;
  std::vector<ActiveChannel> active_channels_;
  std::unique_ptr<WindowsVirtualAsioRuntime> runtime_;
  std::string last_error_;
  ASIOSampleRate sample_rate_ = kDefaultSampleRate;
  long preferred_buffer_frames_ = kPreferredBufferFrames;
  long buffer_frames_ = 0;
  bool initialized_ = false;
  bool buffers_created_ = false;
  bool format_discovered_ = false;
  std::atomic_bool running_ = false;
};

class VirtualAsioClassFactory final : public IClassFactory {
 public:
  VirtualAsioClassFactory() noexcept { module_objects.fetch_add(1); }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (!IsEqualIID(iid, IID_IUnknown) &&
        !IsEqualIID(iid, IID_IClassFactory)) {
      return E_NOINTERFACE;
    }
    *object = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() noexcept override {
    return references_.fetch_add(1) + 1;
  }

  ULONG STDMETHODCALLTYPE Release() noexcept override {
    const auto remaining = references_.fetch_sub(1) - 1;
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,
                                           REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (outer != nullptr) {
      return CLASS_E_NOAGGREGATION;
    }
    auto* driver = new (std::nothrow) VirtualAsioDriver();
    if (driver == nullptr) {
      return E_OUTOFMEMORY;
    }
    const auto result = driver->QueryInterface(iid, object);
    driver->Release();
    return result;
  }

  HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) noexcept override {
    if (lock) {
      server_locks.fetch_add(1);
    } else {
      auto current = server_locks.load();
      while (current != 0 &&
             !server_locks.compare_exchange_weak(current, current - 1)) {
      }
    }
    return S_OK;
  }

 private:
  ~VirtualAsioClassFactory() { module_objects.fetch_sub(1); }

  std::atomic_ulong references_ = 1;
};

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE instance,
                               DWORD reason,
                               void*) noexcept {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

STDAPI DllCanUnloadNow() {
  return module_objects.load() == 0 && server_locks.load() == 0 ? S_OK
                                                                : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID clsid,
                         REFIID iid,
                         void** object) {
  if (object == nullptr) {
    return E_POINTER;
  }
  *object = nullptr;
  if (!IsEqualCLSID(clsid, sar::driver::kWindowsVirtualAsioClsid)) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }
  auto* factory = new (std::nothrow) VirtualAsioClassFactory();
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }
  const auto result = factory->QueryInterface(iid, object);
  factory->Release();
  return result;
}
