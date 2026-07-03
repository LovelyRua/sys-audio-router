#include "core/platform/windows_wasapi_device_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Audioclient.h>
#include <propkeydef.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <Mmdeviceapi.h>
#include <Propidl.h>
#include <Propvarutil.h>

#include <cstdio>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sar::platform {

namespace {

template <typename T>
class ComPtr {
 public:
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;

  ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
    }
    return *this;
  }

  ~ComPtr() {
    reset();
  }

  [[nodiscard]] T* get() const noexcept {
    return ptr_;
  }

  [[nodiscard]] T** put() noexcept {
    reset();
    return &ptr_;
  }

  [[nodiscard]] T* operator->() const noexcept {
    return ptr_;
  }

  explicit operator bool() const noexcept {
    return ptr_ != nullptr;
  }

  void reset() noexcept {
    if (ptr_ != nullptr) {
      ptr_->Release();
      ptr_ = nullptr;
    }
  }

 private:
  T* ptr_ = nullptr;
};

class ComApartment {
 public:
  ComApartment() {
    result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  }

  ~ComApartment() {
    if (result_ == S_OK || result_ == S_FALSE) {
      CoUninitialize();
    }
  }

  [[nodiscard]] HRESULT result() const noexcept {
    return result_;
  }

  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_ = E_FAIL;
};

std::string hresult_hex(HRESULT result) {
  char buffer[16] = {};
  const auto value = static_cast<unsigned long>(result);
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", value);
  return buffer;
}

std::string wide_to_utf8(const wchar_t* value) {
  if (value == nullptr || value[0] == L'\0') {
    return {};
  }

  const auto size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
  if (size <= 1) {
    return {};
  }

  std::string result(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  return result;
}

std::string device_id(IMMDevice& device) {
  wchar_t* id = nullptr;
  if (FAILED(device.GetId(&id)) || id == nullptr) {
    return {};
  }

  auto result = wide_to_utf8(id);
  CoTaskMemFree(id);
  return result;
}

std::string friendly_name(IMMDevice& device) {
  ComPtr<IPropertyStore> properties;
  if (FAILED(device.OpenPropertyStore(STGM_READ, properties.put()))) {
    return {};
  }

  PROPVARIANT value;
  PropVariantInit(&value);
  const auto result = properties->GetValue(PKEY_Device_FriendlyName, &value);
  if (FAILED(result)) {
    PropVariantClear(&value);
    return {};
  }

  std::string name;
  if (value.vt == VT_LPWSTR) {
    name = wide_to_utf8(value.pwszVal);
  }
  PropVariantClear(&value);
  return name;
}

AudioFormat mix_format(IMMDevice& device) {
  AudioFormat format;
  ComPtr<IAudioClient> audio_client;
  if (FAILED(device.Activate(__uuidof(IAudioClient),
                             CLSCTX_ALL,
                             nullptr,
                             reinterpret_cast<void**>(audio_client.put())))) {
    return format;
  }

  WAVEFORMATEX* wave_format = nullptr;
  if (FAILED(audio_client->GetMixFormat(&wave_format)) || wave_format == nullptr) {
    return format;
  }

  format.sample_rate = wave_format->nSamplesPerSec;
  format.channels = wave_format->nChannels;
  format.frames_per_block = 128;
  CoTaskMemFree(wave_format);
  return format;
}

void append_devices(IMMDeviceEnumerator& enumerator,
                    EDataFlow flow,
                    AudioDeviceDirection direction,
                    std::vector<AudioDeviceDescriptor>& devices,
                    std::vector<AudioDeviceError>& errors) {
  ComPtr<IMMDeviceCollection> collection;
  const auto enum_result = enumerator.EnumAudioEndpoints(flow,
                                                        DEVICE_STATE_ACTIVE,
                                                        collection.put());
  if (FAILED(enum_result)) {
    errors.push_back({
        "wasapi_enum_failed",
        "WASAPI endpoint enumeration failed with " + hresult_hex(enum_result) + ".",
    });
    return;
  }

  UINT count = 0;
  if (FAILED(collection->GetCount(&count))) {
    errors.push_back({"wasapi_count_failed", "WASAPI endpoint count query failed."});
    return;
  }

  for (UINT index = 0; index < count; ++index) {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(index, device.put())) || !device) {
      errors.push_back({"wasapi_device_failed", "WASAPI endpoint query failed."});
      continue;
    }

    AudioDeviceDescriptor descriptor;
    descriptor.id = device_id(*device);
    descriptor.label = friendly_name(*device);
    if (descriptor.label.empty()) {
      descriptor.label = descriptor.id;
    }
    descriptor.backend = AudioBackendKind::Wasapi;
    descriptor.direction = direction;
    descriptor.formats.push_back(mix_format(*device));
    descriptor.is_virtual = false;
    devices.push_back(std::move(descriptor));
  }
}

}  // namespace

AudioBackendKind WindowsWasapiDeviceProvider::backend() const noexcept {
  return AudioBackendKind::Wasapi;
}

AudioDeviceListResult WindowsWasapiDeviceProvider::list_devices() const {
  ComApartment apartment;
  if (!apartment.ok()) {
    return AudioDeviceListResult::failure({
        {
            "com_initialize_failed",
            "COM initialization failed with " + hresult_hex(apartment.result()) + ".",
        },
    });
  }

  ComPtr<IMMDeviceEnumerator> enumerator;
  const auto create_result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                             nullptr,
                                             CLSCTX_ALL,
                                             __uuidof(IMMDeviceEnumerator),
                                             reinterpret_cast<void**>(enumerator.put()));
  if (FAILED(create_result) || !enumerator) {
    return AudioDeviceListResult::failure({
        {
            "wasapi_enumerator_failed",
            "WASAPI enumerator creation failed with " + hresult_hex(create_result) + ".",
        },
    });
  }

  std::vector<AudioDeviceDescriptor> devices;
  std::vector<AudioDeviceError> errors;
  append_devices(*enumerator, eRender, AudioDeviceDirection::Output, devices, errors);
  append_devices(*enumerator, eCapture, AudioDeviceDirection::Input, devices, errors);

  auto validation_errors = validate_audio_devices(devices);
  for (const auto& error : validation_errors) {
    errors.push_back(error);
  }

  if (!errors.empty()) {
    return AudioDeviceListResult::failure(std::move(errors));
  }
  return AudioDeviceListResult::success(std::move(devices));
}

}  // namespace sar::platform
