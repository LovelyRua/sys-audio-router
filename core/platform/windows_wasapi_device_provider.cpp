#include "core/platform/windows_wasapi_device_provider.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Audioclient.h>
#include <mmreg.h>
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

  [[nodiscard]] T& operator*() const noexcept {
    return *ptr_;
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

std::string default_device_id(IMMDeviceEnumerator& enumerator, EDataFlow flow) {
  ComPtr<IMMDevice> device;
  if (FAILED(enumerator.GetDefaultAudioEndpoint(flow, eConsole, device.put())) || !device) {
    return {};
  }
  return device_id(*device);
}

bool is_wave_subformat(const GUID& guid, unsigned long data1) noexcept {
  return guid.Data1 == data1 && guid.Data2 == 0x0000 && guid.Data3 == 0x0010 &&
         guid.Data4[0] == 0x80 && guid.Data4[1] == 0x00 &&
         guid.Data4[2] == 0x00 && guid.Data4[3] == 0xaa &&
         guid.Data4[4] == 0x00 && guid.Data4[5] == 0x38 &&
         guid.Data4[6] == 0x9b && guid.Data4[7] == 0x71;
}

AudioSampleFormat sample_format(const WAVEFORMATEX& wave_format) noexcept {
  if (wave_format.wFormatTag == WAVE_FORMAT_PCM) {
    return AudioSampleFormat::PcmInt;
  }
  if (wave_format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
    return AudioSampleFormat::IeeeFloat;
  }
  if (wave_format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
      wave_format.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    return AudioSampleFormat::Unknown;
  }

  const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(wave_format);
  if (is_wave_subformat(extensible.SubFormat, 0x00000001)) {
    return AudioSampleFormat::PcmInt;
  }
  if (is_wave_subformat(extensible.SubFormat, 0x00000003)) {
    return AudioSampleFormat::IeeeFloat;
  }
  return AudioSampleFormat::Unknown;
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
  format.bits_per_sample = wave_format->wBitsPerSample;
  format.sample_format = sample_format(*wave_format);
  format.frames_per_block = 128;
  CoTaskMemFree(wave_format);
  return format;
}

void append_devices(IMMDeviceEnumerator& enumerator,
                    EDataFlow flow,
                    AudioDeviceDirection direction,
                    const std::string& default_id,
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
    descriptor.is_default = descriptor.id == default_id;
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
  const auto default_render_id = default_device_id(*enumerator, eRender);
  const auto default_capture_id = default_device_id(*enumerator, eCapture);
  append_devices(*enumerator,
                 eRender,
                 AudioDeviceDirection::Output,
                 default_render_id,
                 devices,
                 errors);
  append_devices(*enumerator,
                 eCapture,
                 AudioDeviceDirection::Input,
                 default_capture_id,
                 devices,
                 errors);

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
