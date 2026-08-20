#include "core/platform/windows_asio_driver_probe.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>

#include "third_party/asio_sdk_2.3.4/common/iasiodrv.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <utility>

namespace sar::platform {

namespace {

bool asio_ok(ASIOError error) noexcept {
  return error == ASE_OK || error == ASE_SUCCESS;
}

std::string hex_error(unsigned long value) {
  char buffer[16]{};
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", value);
  return buffer;
}

class ComApartment {
 public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() {
    if (result_ == S_OK || result_ == S_FALSE) {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }
  [[nodiscard]] HRESULT result() const noexcept { return result_; }

 private:
  HRESULT result_ = E_FAIL;
};

class AsioDriverPtr {
 public:
  AsioDriverPtr() = default;
  AsioDriverPtr(const AsioDriverPtr&) = delete;
  AsioDriverPtr& operator=(const AsioDriverPtr&) = delete;
  ~AsioDriverPtr() {
    if (driver_ != nullptr) {
      driver_->Release();
    }
  }

  [[nodiscard]] IASIO** put() noexcept { return &driver_; }
  [[nodiscard]] IASIO* operator->() const noexcept { return driver_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return driver_ != nullptr;
  }

 private:
  IASIO* driver_ = nullptr;
};

void set_sample_type(ASIOSampleType type,
                     AudioSampleFormat& format,
                     std::uint32_t& bits) noexcept {
  switch (type) {
    case ASIOSTInt16LSB:
    case ASIOSTInt16MSB:
      format = AudioSampleFormat::PcmInt;
      bits = 16;
      break;
    case ASIOSTInt24LSB:
    case ASIOSTInt24MSB:
      format = AudioSampleFormat::PcmInt;
      bits = 24;
      break;
    case ASIOSTInt32LSB:
    case ASIOSTInt32MSB:
    case ASIOSTInt32LSB16:
    case ASIOSTInt32LSB18:
    case ASIOSTInt32LSB20:
    case ASIOSTInt32LSB24:
    case ASIOSTInt32MSB16:
    case ASIOSTInt32MSB18:
    case ASIOSTInt32MSB20:
    case ASIOSTInt32MSB24:
      format = AudioSampleFormat::PcmInt;
      bits = 32;
      break;
    case ASIOSTFloat32LSB:
    case ASIOSTFloat32MSB:
      format = AudioSampleFormat::IeeeFloat;
      bits = 32;
      break;
    case ASIOSTFloat64LSB:
    case ASIOSTFloat64MSB:
      format = AudioSampleFormat::IeeeFloat;
      bits = 64;
      break;
    default:
      break;
  }
}

}  // namespace

WindowsAsioDriverProbeResult WindowsAsioDriverProbeResult::success(
    WindowsAsioDriverProbe probe) {
  return {std::move(probe), {}, true};
}

WindowsAsioDriverProbeResult WindowsAsioDriverProbeResult::failure(
    AudioDeviceError error) {
  return {{}, std::move(error), false};
}

bool WindowsAsioDriverProbeResult::ok() const noexcept { return succeeded_; }

const WindowsAsioDriverProbe& WindowsAsioDriverProbeResult::probe()
    const noexcept {
  return probe_;
}

const AudioDeviceError& WindowsAsioDriverProbeResult::error() const noexcept {
  return error_;
}

WindowsAsioDriverProbeResult::WindowsAsioDriverProbeResult(
    WindowsAsioDriverProbe probe,
    AudioDeviceError error,
    bool succeeded) noexcept
    : probe_(std::move(probe)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

WindowsAsioDriverProbeResult probe_windows_asio_driver(
    const std::string& clsid_text) {
  if (clsid_text.empty()) {
    return WindowsAsioDriverProbeResult::failure(
        {"asio_empty_clsid", "ASIO driver CLSID is empty."});
  }

  const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                             clsid_text.data(),
                                             static_cast<int>(clsid_text.size()),
                                             nullptr, 0);
  if (wide_size <= 0) {
    return WindowsAsioDriverProbeResult::failure(
        {"asio_invalid_clsid", "ASIO driver CLSID is not valid UTF-8."});
  }
  std::wstring wide_clsid(static_cast<std::size_t>(wide_size), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, clsid_text.data(),
                      static_cast<int>(clsid_text.size()), wide_clsid.data(),
                      wide_size);

  CLSID clsid{};
  if (FAILED(CLSIDFromString(wide_clsid.c_str(), &clsid))) {
    return WindowsAsioDriverProbeResult::failure(
        {"asio_invalid_clsid", "ASIO driver CLSID has invalid syntax."});
  }

  ComApartment apartment;
  if (!apartment.ok()) {
    return WindowsAsioDriverProbeResult::failure({
        "asio_com_initialization_failed",
        "COM initialization failed with " +
            hex_error(static_cast<unsigned long>(apartment.result())) + ".",
    });
  }

  AsioDriverPtr driver;
  const auto activation = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER,
                                           clsid,
                                           reinterpret_cast<void**>(driver.put()));
  if (FAILED(activation) || !driver) {
    return WindowsAsioDriverProbeResult::failure({
        "asio_activation_failed",
        "ASIO driver activation failed with " +
            hex_error(static_cast<unsigned long>(activation)) + ".",
    });
  }
  if (driver->init(GetDesktopWindow()) == ASIOFalse) {
    char error[128]{};
    driver->getErrorMessage(error);
    return WindowsAsioDriverProbeResult::failure({
        "asio_initialization_failed",
        error[0] == '\0' ? "ASIO driver initialization failed."
                         : std::string(error),
    });
  }

  long inputs = 0;
  long outputs = 0;
  long minimum = 0;
  long maximum = 0;
  long preferred = 0;
  long granularity = 0;
  if (!asio_ok(driver->getChannels(&inputs, &outputs)) || inputs < 0 ||
      outputs < 0 || (inputs == 0 && outputs == 0)) {
    return WindowsAsioDriverProbeResult::failure(
        {"asio_channel_query_failed", "ASIO channel query failed."});
  }
  if (!asio_ok(driver->getBufferSize(&minimum, &maximum, &preferred,
                                     &granularity)) ||
      minimum <= 0 || maximum < minimum || preferred < minimum ||
      preferred > maximum) {
    return WindowsAsioDriverProbeResult::failure(
        {"asio_buffer_query_failed", "ASIO buffer-size query failed."});
  }

  WindowsAsioDriverProbe probe;
  probe.clsid = clsid_text;
  char name[128]{};
  driver->getDriverName(name);
  probe.driver_name = name;
  probe.driver_version = driver->getDriverVersion();
  probe.input_channels = static_cast<std::uint32_t>(inputs);
  probe.output_channels = static_cast<std::uint32_t>(outputs);
  probe.minimum_buffer_frames = static_cast<std::uint32_t>(minimum);
  probe.maximum_buffer_frames = static_cast<std::uint32_t>(maximum);
  probe.preferred_buffer_frames = static_cast<std::uint32_t>(preferred);
  probe.buffer_granularity = granularity;

  ASIOSampleRate current_rate = 0.0;
  if (asio_ok(driver->getSampleRate(&current_rate)) &&
      std::isfinite(current_rate) && current_rate > 0.0) {
    probe.current_sample_rate = current_rate;
  }
  constexpr std::array<std::uint32_t, 6> candidate_rates{
      44100, 48000, 88200, 96000, 176400, 192000};
  for (const auto rate : candidate_rates) {
    if (asio_ok(driver->canSampleRate(static_cast<ASIOSampleRate>(rate)))) {
      probe.supported_sample_rates.push_back(rate);
    }
  }

  ASIOChannelInfo channel{};
  if (inputs > 0) {
    channel.channel = 0;
    channel.isInput = ASIOTrue;
  } else {
    channel.channel = 0;
    channel.isInput = ASIOFalse;
  }
  if (asio_ok(driver->getChannelInfo(&channel))) {
    set_sample_type(channel.type, probe.sample_format, probe.bits_per_sample);
  }
  if (probe.sample_format == AudioSampleFormat::Unknown) {
    return WindowsAsioDriverProbeResult::failure({
        "asio_sample_type_unsupported",
        "ASIO driver channel sample type is not supported.",
    });
  }
  if (probe.supported_sample_rates.empty() &&
      probe.current_sample_rate > 0.0 &&
      probe.current_sample_rate <=
          static_cast<double>(UINT32_MAX)) {
    probe.supported_sample_rates.push_back(
        static_cast<std::uint32_t>(std::llround(probe.current_sample_rate)));
  }
  if (probe.supported_sample_rates.empty()) {
    return WindowsAsioDriverProbeResult::failure({
        "asio_sample_rate_query_failed",
        "ASIO driver did not report a supported sample rate.",
    });
  }
  return WindowsAsioDriverProbeResult::success(std::move(probe));
}

}  // namespace sar::platform
