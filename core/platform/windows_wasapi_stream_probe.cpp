#include "core/platform/windows_wasapi_stream_probe.h"

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
#include <string>
#include <utility>

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

  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
  result.pop_back();
  return result;
}

std::wstring utf8_to_wide(const std::string& value) {
  if (value.empty()) {
    return {};
  }

  const auto size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
  if (size <= 1) {
    return {};
  }

  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          value.c_str(),
                          -1,
                          result.data(),
                          size) != size) {
    return {};
  }
  result.pop_back();
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

EDataFlow data_flow(WasapiStreamDirection direction) noexcept {
  return direction == WasapiStreamDirection::Render ? eRender : eCapture;
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

AudioFormat to_audio_format(const WAVEFORMATEX& format) {
  AudioFormat result;
  result.sample_rate = format.nSamplesPerSec;
  result.channels = format.nChannels;
  result.bits_per_sample = format.wBitsPerSample;
  if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
      format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
    const auto& extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
    result.valid_bits_per_sample = extensible.Samples.wValidBitsPerSample;
  }
  result.sample_format = sample_format(format);
  result.frames_per_block = 128;
  return result;
}

}  // namespace

const char* wasapi_stream_direction_name(WasapiStreamDirection direction) noexcept {
  switch (direction) {
    case WasapiStreamDirection::Render:
      return "render";
    case WasapiStreamDirection::Capture:
      return "capture";
  }
  return "unknown";
}

const char* wasapi_stream_mode_name(WasapiStreamMode mode) noexcept {
  switch (mode) {
    case WasapiStreamMode::Endpoint:
      return "endpoint";
    case WasapiStreamMode::Loopback:
      return "loopback";
  }
  return "unknown";
}

WasapiStreamProbeResult WasapiStreamProbeResult::success(WasapiStreamProbe probe) {
  return {std::move(probe), {}};
}

WasapiStreamProbeResult WasapiStreamProbeResult::failure(
    std::vector<WasapiStreamProbeError> errors) {
  return {{}, std::move(errors)};
}

bool WasapiStreamProbeResult::ok() const noexcept {
  return errors_.empty();
}

const WasapiStreamProbe& WasapiStreamProbeResult::probe() const noexcept {
  return probe_;
}

const std::vector<WasapiStreamProbeError>& WasapiStreamProbeResult::errors() const noexcept {
  return errors_;
}

WasapiStreamProbeResult::WasapiStreamProbeResult(
    WasapiStreamProbe probe,
    std::vector<WasapiStreamProbeError> errors)
    : probe_(std::move(probe)), errors_(std::move(errors)) {}

namespace {

WasapiStreamProbeResult probe_wasapi_stream_impl(
    const std::string* requested_device_id,
    WasapiStreamDirection direction,
    WasapiStreamMode mode) {
  if (mode == WasapiStreamMode::Loopback &&
      direction != WasapiStreamDirection::Capture) {
    return WasapiStreamProbeResult::failure({
        {
            "invalid_loopback_direction",
            "WASAPI loopback mode is only valid for capture streams.",
        },
    });
  }

  std::wstring wide_requested_device_id;
  if (requested_device_id != nullptr) {
    wide_requested_device_id = utf8_to_wide(*requested_device_id);
    if (wide_requested_device_id.empty()) {
      return WasapiStreamProbeResult::failure({
          {
              "invalid_device_id_encoding",
              "WASAPI device ID must be valid non-empty UTF-8.",
          },
      });
    }
  }

  ComApartment apartment;
  if (!apartment.ok()) {
    return WasapiStreamProbeResult::failure({
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
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_enumerator_failed",
            "WASAPI enumerator creation failed with " + hresult_hex(create_result) + ".",
        },
    });
  }

  ComPtr<IMMDevice> device;
  HRESULT endpoint_result = E_FAIL;
  if (requested_device_id == nullptr) {
    const auto endpoint_direction = mode == WasapiStreamMode::Loopback
                                        ? WasapiStreamDirection::Render
                                        : direction;
    endpoint_result = enumerator->GetDefaultAudioEndpoint(
        data_flow(endpoint_direction), eConsole, device.put());
  } else {
    endpoint_result =
        enumerator->GetDevice(wide_requested_device_id.c_str(), device.put());
  }
  if (FAILED(endpoint_result) || !device) {
    const auto code = requested_device_id == nullptr
                          ? "wasapi_default_endpoint_failed"
                          : "wasapi_device_lookup_failed";
    const auto operation = requested_device_id == nullptr
                               ? "default endpoint query"
                               : "device lookup";
    return WasapiStreamProbeResult::failure({
        {
            code,
            "WASAPI " + std::string(operation) + " failed with " +
                hresult_hex(endpoint_result) + ".",
        },
    });
  }

  ComPtr<IAudioClient> audio_client;
  const auto activate_result = device->Activate(__uuidof(IAudioClient),
                                               CLSCTX_ALL,
                                               nullptr,
                                               reinterpret_cast<void**>(audio_client.put()));
  if (FAILED(activate_result) || !audio_client) {
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_audio_client_failed",
            "WASAPI audio client activation failed with " + hresult_hex(activate_result) + ".",
        },
    });
  }

  WAVEFORMATEX* wave_format = nullptr;
  const auto format_result = audio_client->GetMixFormat(&wave_format);
  if (FAILED(format_result) || wave_format == nullptr) {
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_mix_format_failed",
            "WASAPI mix format query failed with " + hresult_hex(format_result) + ".",
        },
    });
  }

  REFERENCE_TIME default_period = 0;
  REFERENCE_TIME minimum_period = 0;
  const auto period_result = audio_client->GetDevicePeriod(&default_period, &minimum_period);
  if (FAILED(period_result)) {
    CoTaskMemFree(wave_format);
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_device_period_failed",
            "WASAPI device period query failed with " + hresult_hex(period_result) + ".",
        },
    });
  }

  const auto stream_flags = mode == WasapiStreamMode::Loopback
                                ? AUDCLNT_STREAMFLAGS_LOOPBACK
                                : 0;
  const auto init_result = audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                   stream_flags,
                                                   0,
                                                   0,
                                                   wave_format,
                                                   nullptr);
  if (FAILED(init_result)) {
    CoTaskMemFree(wave_format);
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_initialize_failed",
            "WASAPI shared stream initialization failed with " + hresult_hex(init_result) + ".",
        },
    });
  }

  UINT32 buffer_frames = 0;
  const auto buffer_result = audio_client->GetBufferSize(&buffer_frames);
  if (FAILED(buffer_result)) {
    CoTaskMemFree(wave_format);
    return WasapiStreamProbeResult::failure({
        {
            "wasapi_buffer_size_failed",
            "WASAPI buffer size query failed with " + hresult_hex(buffer_result) + ".",
        },
    });
  }

  WasapiStreamProbe probe;
  probe.direction = direction;
  probe.mode = mode;
  probe.device_id = device_id(*device);
  probe.device_label = friendly_name(*device);
  if (probe.device_label.empty()) {
    probe.device_label = probe.device_id;
  }
  probe.mix_format = to_audio_format(*wave_format);
  probe.mix_format.frames_per_block = buffer_frames;
  probe.default_period_100ns = static_cast<std::uint64_t>(default_period);
  probe.minimum_period_100ns = static_cast<std::uint64_t>(minimum_period);
  probe.buffer_frames = buffer_frames;

  CoTaskMemFree(wave_format);
  return WasapiStreamProbeResult::success(std::move(probe));
}

}  // namespace

WasapiStreamProbeResult probe_default_wasapi_stream(WasapiStreamDirection direction,
                                                    WasapiStreamMode mode) {
  return probe_wasapi_stream_impl(nullptr, direction, mode);
}

WasapiStreamProbeResult probe_wasapi_stream(const std::string& device_id,
                                            WasapiStreamDirection direction,
                                            WasapiStreamMode mode) {
  return probe_wasapi_stream_impl(&device_id, direction, mode);
}

}  // namespace sar::platform
