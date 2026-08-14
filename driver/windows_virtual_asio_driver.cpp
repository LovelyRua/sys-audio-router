#include "driver/windows_virtual_asio_com.h"
#include "driver/windows_virtual_asio_runtime.h"

#include <Unknwn.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <atomic>
#include <array>
#include <cmath>
#include <cstdint>
#include <condition_variable>
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

constexpr long kDefaultInputChannels = 2;
constexpr long kDefaultOutputChannels = 2;
constexpr long kMinimumBufferFrames = 64;
constexpr long kMaximumBufferFrames = 2048;
constexpr long kPreferredBufferFrames = 256;
constexpr double kDefaultSampleRate = 48000.0;

struct VirtualAsioInstanceDescriptor {
  CLSID clsid{};
  std::wstring display_name;
  std::wstring broker_pipe_name;
};

std::wstring clsid_string(REFCLSID clsid) {
  std::array<wchar_t, 40> value{};
  const auto length = StringFromGUID2(clsid, value.data(),
                                      static_cast<int>(value.size()));
  return length > 1 ? std::wstring(value.data(), length - 1) : std::wstring{};
}

bool read_registry_string(HKEY key,
                          const wchar_t* name,
                          std::wstring& value) {
  DWORD type = 0;
  DWORD bytes = 0;
  auto result = RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes);
  if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) ||
      bytes < sizeof(wchar_t)) {
    return false;
  }
  std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
  result = RegQueryValueExW(key, name, nullptr, &type,
                            reinterpret_cast<BYTE*>(buffer.data()), &bytes);
  if (result != ERROR_SUCCESS) {
    return false;
  }
  value.assign(buffer.data());
  return !value.empty();
}

bool resolve_instance(REFCLSID clsid, VirtualAsioInstanceDescriptor& instance) {
  const auto text = clsid_string(clsid);
  if (text.empty()) {
    return false;
  }
  const auto key_path = std::wstring(L"CLSID\\") + text;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key_path.c_str(), 0, KEY_QUERY_VALUE,
                    &key) == ERROR_SUCCESS) {
    std::wstring display_name;
    std::wstring broker_token;
    const auto valid = read_registry_string(key, nullptr, display_name) &&
                       read_registry_string(key, L"BrokerToken", broker_token);
    RegCloseKey(key);
    if (valid && broker_token.find_first_of(L"\\/") == std::wstring::npos) {
      instance.clsid = clsid;
      instance.display_name = std::move(display_name);
      instance.broker_pipe_name =
          std::wstring(L"sys-audio-route-control-") + broker_token;
      return true;
    }
  }
  if (!IsEqualCLSID(clsid, sar::driver::kWindowsVirtualAsioClsid)) {
    return false;
  }
  instance.clsid = clsid;
  instance.display_name = sar::driver::kWindowsVirtualAsioDisplayName;
  // Keep the environment override available to unregistered development and
  // smoke-test loads of the legacy CLSID.
  instance.broker_pipe_name.clear();
  return true;
}

std::string narrow_display_name(const std::wstring& value) {
  if (value.empty()) {
    return "System Audio Route";
  }
  const auto required = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1,
                                             nullptr, 0, nullptr, nullptr);
  if (required <= 1) {
    return "System Audio Route";
  }
  std::string result(static_cast<std::size_t>(required), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, result.data(), required,
                      nullptr, nullptr);
  result.pop_back();
  return result;
}

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
  explicit VirtualAsioDriver(VirtualAsioInstanceDescriptor instance)
      : instance_(std::move(instance)),
        driver_name_(narrow_display_name(instance_.display_name)) {
    module_objects.fetch_add(1);
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                           void** object) noexcept override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    // ASIO hosts conventionally request the driver's CLSID as its interface
    // identifier because IASIO does not define a separate COM IID.
    if (!IsEqualIID(iid, IID_IUnknown) &&
        !IsEqualIID(iid, instance_.clsid)) {
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
    const auto format = WindowsVirtualAsioRuntime::query_engine_format(
        instance_.broker_pipe_name);
    if (format.ok() && format.format.input_channels != 0 &&
        format.format.output_channels != 0 &&
        format.format.input_channels <=
            static_cast<std::uint32_t>(std::numeric_limits<long>::max()) &&
        format.format.output_channels <=
            static_cast<std::uint32_t>(std::numeric_limits<long>::max()) &&
        format.format.sample_rate != 0 &&
        format.format.frames_per_block <=
            static_cast<std::uint32_t>(std::numeric_limits<long>::max()) &&
        supported_buffer_size(
            static_cast<long>(format.format.frames_per_block))) {
      sample_rate_ = static_cast<ASIOSampleRate>(format.format.sample_rate);
      preferred_buffer_frames_ =
          static_cast<long>(format.format.frames_per_block);
      input_channels_ = static_cast<long>(format.format.input_channels);
      output_channels_ = static_cast<long>(format.format.output_channels);
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
    copy_asio_string(name, 32, driver_name_.c_str());
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
    std::unique_lock lock(control_mutex_);
    runtime_operation_cv_.wait(lock, [this] { return !runtime_operation_; });
    if (!initialized_ || !buffers_created_ || runtime_ == nullptr) {
      return fail(ASE_InvalidMode,
                  "The driver must be initialized and buffers created before start.");
    }
    runtime_operation_ = true;
    auto* runtime = runtime_.get();
    lock.unlock();
    std::string error;
    const auto started = runtime->start(error);
    lock.lock();
    runtime_operation_ = false;
    runtime_operation_cv_.notify_all();
    if (!started) {
      return fail(ASE_HWMalfunction, error.c_str());
    }
    running_.store(true, std::memory_order_release);
    last_error_.clear();
    return ASE_OK;
  }

  ASIOError stop() override {
    std::unique_lock lock(control_mutex_);
    runtime_operation_cv_.wait(lock, [this] { return !runtime_operation_; });
    auto* runtime = runtime_.get();
    runtime_operation_ = true;
    running_.store(false, std::memory_order_release);
    lock.unlock();
    if (runtime != nullptr) {
      runtime->stop();
    }
    lock.lock();
    runtime_operation_ = false;
    runtime_operation_cv_.notify_all();
    return ASE_OK;
  }

  ASIOError getChannels(long* input_channels,
                        long* output_channels) override {
    if (input_channels == nullptr || output_channels == nullptr) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "Channel count output pointers must not be null.");
    }
    std::scoped_lock lock(control_mutex_);
    *input_channels = input_channels_;
    *output_channels = output_channels_;
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
      *minimum = kMinimumBufferFrames;
      *maximum = kMaximumBufferFrames;
      *preferred = preferred_buffer_frames_;
      *granularity = -1;
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
    std::scoped_lock lock(control_mutex_);
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
    std::scoped_lock lock(control_mutex_);
    const auto channel_count = info->isInput == ASIOTrue ? input_channels_
                                                         : output_channels_;
    if (info->channel < 0 || info->channel >= channel_count) {
      return fail_thread_safe(ASE_InvalidParameter,
                              "The requested channel does not exist.");
    }
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
    if (!supported_buffer_size(buffer_frames)) {
      return fail_thread_safe(ASE_InvalidMode,
                              "The requested buffer size is not supported.");
    }

    std::unique_lock lock(control_mutex_);
    runtime_operation_cv_.wait(lock, [this] { return !runtime_operation_; });
    if (!initialized_ || running_.load(std::memory_order_acquire) ||
        buffers_created_) {
      return fail(ASE_InvalidMode,
                  "Buffers require an initialized, stopped, unprepared driver.");
    }
    if (channel_count > input_channels_ + output_channels_) {
      return fail(ASE_InvalidParameter,
                  "The requested active channel count is too large.");
    }
    try {
      buffers_.assign(static_cast<std::size_t>(channel_count) * 2U, {});
      active_channels_.clear();
      active_channels_.reserve(static_cast<std::size_t>(channel_count));
      for (long index = 0; index < channel_count; ++index) {
        const auto& descriptor = buffer_infos[index];
        const auto available = descriptor.isInput == ASIOTrue
                                   ? input_channels_
                                   : output_channels_;
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
        .broker_pipe_name = instance_.broker_pipe_name,
        .sample_rate = static_cast<std::uint32_t>(sample_rate_),
        .frames_per_block = static_cast<std::uint32_t>(buffer_frames),
        .input_channels = static_cast<std::uint32_t>(input_channels_),
        .output_channels = static_cast<std::uint32_t>(output_channels_),
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
    std::unique_lock lock(control_mutex_);
    runtime_operation_cv_.wait(lock, [this] { return !runtime_operation_; });
    if (!buffers_created_) {
      return ASE_InvalidMode;
    }
    running_.store(false, std::memory_order_release);
    runtime_operation_ = true;
    auto runtime = std::move(runtime_);
    lock.unlock();
    // Runtime destruction owns the full stop/disconnect sequence. Keeping it
    // in one place prevents a second stop from racing a concurrent callback.
    runtime.reset();
    lock.lock();
    reset_buffers();
    runtime_operation_ = false;
    runtime_operation_cv_.notify_all();
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
    buffers_.clear();
    active_channels_.clear();
    callbacks_ = nullptr;
    buffer_frames_ = 0;
    buffers_created_ = false;
  }

  std::atomic_ulong references_ = 1;
  VirtualAsioInstanceDescriptor instance_;
  std::string driver_name_;
  std::mutex control_mutex_;
  std::condition_variable runtime_operation_cv_;
  void* system_handle_ = nullptr;
  ASIOCallbacks* callbacks_ = nullptr;
  std::vector<std::vector<float>> buffers_;
  std::vector<ActiveChannel> active_channels_;
  std::unique_ptr<WindowsVirtualAsioRuntime> runtime_;
  std::string last_error_;
  ASIOSampleRate sample_rate_ = kDefaultSampleRate;
  long preferred_buffer_frames_ = kPreferredBufferFrames;
  long buffer_frames_ = 0;
  long input_channels_ = kDefaultInputChannels;
  long output_channels_ = kDefaultOutputChannels;
  bool initialized_ = false;
  bool buffers_created_ = false;
  bool format_discovered_ = false;
  bool runtime_operation_ = false;
  std::atomic_bool running_ = false;
};

class VirtualAsioClassFactory final : public IClassFactory {
 public:
  explicit VirtualAsioClassFactory(VirtualAsioInstanceDescriptor instance)
      : instance_(std::move(instance)) {
    module_objects.fetch_add(1);
  }

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
    VirtualAsioDriver* driver = nullptr;
    try {
      driver = new (std::nothrow) VirtualAsioDriver(instance_);
    } catch (const std::bad_alloc&) {
      return E_OUTOFMEMORY;
    }
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
  VirtualAsioInstanceDescriptor instance_;
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
  VirtualAsioClassFactory* factory = nullptr;
  try {
    VirtualAsioInstanceDescriptor instance;
    if (!resolve_instance(clsid, instance)) {
      return CLASS_E_CLASSNOTAVAILABLE;
    }
    factory =
        new (std::nothrow) VirtualAsioClassFactory(std::move(instance));
  } catch (const std::bad_alloc&) {
    return E_OUTOFMEMORY;
  }
  if (factory == nullptr) {
    return E_OUTOFMEMORY;
  }
  const auto result = factory->QueryInterface(iid, object);
  factory->Release();
  return result;
}
