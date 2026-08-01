#include "core/platform/windows_wasapi_stream.h"

#include "core/platform/sample_converter.h"

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
#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace sar::platform {

std::uint32_t select_wasapi_shared_period_frames(
    std::uint32_t requested,
    std::uint32_t default_period,
    std::uint32_t fundamental_period,
    std::uint32_t minimum_period,
    std::uint32_t maximum_period) noexcept {
  if (requested == 0 || minimum_period == 0 || maximum_period < minimum_period) {
    return default_period;
  }
  if (fundamental_period == 0) {
    return default_period;
  }
  const auto first =
      ((static_cast<std::uint64_t>(minimum_period) + fundamental_period - 1) /
       fundamental_period) * fundamental_period;
  const auto last =
      (static_cast<std::uint64_t>(maximum_period) / fundamental_period) *
      fundamental_period;
  if (first > last || first > std::numeric_limits<std::uint32_t>::max()) {
    return default_period;
  }
  if (requested <= first) {
    return static_cast<std::uint32_t>(first);
  }
  if (requested >= last) {
    return static_cast<std::uint32_t>(last);
  }
  const auto lower =
      (static_cast<std::uint64_t>(requested) / fundamental_period) *
      fundamental_period;
  const auto upper = lower + fundamental_period;
  return requested - lower < upper - requested
             ? static_cast<std::uint32_t>(lower)
             : static_cast<std::uint32_t>(upper);
}

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

class UniqueHandle {
 public:
  UniqueHandle() = default;
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;

  UniqueHandle(UniqueHandle&& other) noexcept
      : handle_(std::exchange(other.handle_, nullptr)) {}

  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
  }

  ~UniqueHandle() {
    reset();
  }

  [[nodiscard]] HANDLE get() const noexcept {
    return handle_;
  }

  [[nodiscard]] bool valid() const noexcept {
    return handle_ != nullptr;
  }

  void reset(HANDLE handle = nullptr) noexcept {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
    handle_ = handle;
  }

 private:
  HANDLE handle_ = nullptr;
};

std::string hresult_hex(HRESULT result) {
  char buffer[16] = {};
  const auto value = static_cast<unsigned long>(result);
  std::snprintf(buffer, sizeof(buffer), "0x%08lX", value);
  return buffer;
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

std::vector<WasapiStreamError> validate_probe(const WasapiStreamProbe& probe) {
  std::vector<WasapiStreamError> errors;

  if (probe.mode == WasapiStreamMode::Loopback &&
      probe.direction != WasapiStreamDirection::Capture) {
    errors.push_back({
        "invalid_loopback_direction",
        "WASAPI loopback mode is only valid for capture streams.",
    });
  }
  if (probe.device_id.empty()) {
    errors.push_back({"empty_device_id", "WASAPI stream probe device ID must not be empty."});
  }
  if (probe.device_label.empty()) {
    errors.push_back({"empty_device_label", "WASAPI stream probe device label must not be empty."});
  }
  if (probe.mix_format.sample_rate == 0) {
    errors.push_back({"invalid_sample_rate", "WASAPI stream sample rate must be non-zero."});
  }
  if (probe.mix_format.channels == 0) {
    errors.push_back({"invalid_channel_count", "WASAPI stream channel count must be non-zero."});
  }
  if (probe.mix_format.bits_per_sample == 0) {
    errors.push_back({"invalid_bits_per_sample", "WASAPI stream bit depth must be non-zero."});
  }
  if (probe.mix_format.sample_format == AudioSampleFormat::Unknown) {
    errors.push_back({
        "unsupported_sample_format",
        "WASAPI stream sample format must be supported before opening.",
    });
  }
  if (probe.mix_format.frames_per_block == 0) {
    errors.push_back({"invalid_frames_per_block", "WASAPI stream block size must be non-zero."});
  }
  if (probe.mix_format.frames_per_block != 0 &&
      probe.mix_format.frames_per_block != probe.buffer_frames) {
    errors.push_back({
        "frames_per_block_mismatch",
        "WASAPI stream block size must match the probed buffer size.",
    });
  }
  if (probe.buffer_frames == 0) {
    errors.push_back({"invalid_buffer_frames", "WASAPI stream buffer size must be non-zero."});
  }
  if (probe.default_period_100ns == 0) {
    errors.push_back({"invalid_device_period", "WASAPI stream device period must be non-zero."});
  }
  if (probe.minimum_period_100ns == 0) {
    errors.push_back({
        "invalid_minimum_device_period",
        "WASAPI stream minimum device period must be non-zero.",
    });
  }
  if (probe.default_period_100ns != 0 &&
      probe.minimum_period_100ns > probe.default_period_100ns) {
    errors.push_back({
        "invalid_device_period_order",
        "WASAPI stream minimum device period must not exceed the default period.",
    });
  }

  return errors;
}

std::vector<WasapiStreamError> convert_probe_errors(
    const std::vector<WasapiStreamProbeError>& errors) {
  std::vector<WasapiStreamError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

constexpr WasapiRealtimeErrorRecord realtime_error(
    WasapiRealtimeErrorCode code,
    WasapiRealtimeErrorContext context = WasapiRealtimeErrorContext::None,
    std::uint32_t value = 0) noexcept {
  return {static_cast<std::uint16_t>(code),
          static_cast<std::uint16_t>(context), value};
}

WasapiRealtimeErrorRecord sample_conversion_error(
    SampleConversionStatus status) noexcept {
  switch (status) {
    case SampleConversionStatus::Ok:
      return {};
    case SampleConversionStatus::UnsupportedFormat:
      return realtime_error(WasapiRealtimeErrorCode::UnsupportedSampleFormat);
    case SampleConversionStatus::BufferTooSmall:
      return realtime_error(WasapiRealtimeErrorCode::SampleBufferTooSmall);
    case SampleConversionStatus::ChannelMismatch:
      return realtime_error(WasapiRealtimeErrorCode::SampleChannelMismatch);
  }
  return realtime_error(WasapiRealtimeErrorCode::SampleConversionFailed);
}

}  // namespace

const char* wasapi_stream_state_name(WasapiStreamState state) noexcept {
  switch (state) {
    case WasapiStreamState::Closed:
      return "closed";
    case WasapiStreamState::Open:
      return "open";
    case WasapiStreamState::Started:
      return "started";
  }
  return "unknown";
}

const char* wasapi_stream_io_status_name(WasapiStreamIoStatus status) noexcept {
  switch (status) {
    case WasapiStreamIoStatus::Completed:
      return "completed";
    case WasapiStreamIoStatus::Idle:
      return "idle";
    case WasapiStreamIoStatus::TimedOut:
      return "timed_out";
    case WasapiStreamIoStatus::Cancelled:
      return "cancelled";
    case WasapiStreamIoStatus::Failed:
      return "failed";
  }
  return "unknown";
}

struct WindowsWasapiStream::Impl {
  ComApartment apartment;
  ComPtr<IMMDeviceEnumerator> enumerator;
  ComPtr<IMMDevice> device;
  ComPtr<IAudioClient> audio_client;
  ComPtr<IAudioClient3> audio_client3;
  ComPtr<IAudioClock> audio_clock;
  ComPtr<IAudioRenderClient> render_client;
  ComPtr<IAudioCaptureClient> capture_client;
  UINT64 clock_frequency = 0;
  UniqueHandle samples_ready_event;
  UniqueHandle stop_requested_event;
  WAVEFORMATEX* wave_format = nullptr;
  bool samples_ready_latched = false;

  ~Impl() {
    if (wave_format != nullptr) {
      CoTaskMemFree(wave_format);
      wave_format = nullptr;
    }
  }
};

namespace {

enum class WasapiEventWaitStatus {
  SamplesReady,
  StopRequested,
  TimedOut,
  Failed,
};

struct WasapiEventWaitResult {
  WasapiEventWaitStatus status = WasapiEventWaitStatus::Failed;
  std::uint32_t native_win32_code = ERROR_SUCCESS;
};

WasapiEventWaitResult wait_for_stream_event(HANDLE samples_ready_event,
                                            HANDLE stop_requested_event,
                                            std::uint32_t timeout_ms) noexcept {
  HANDLE events[] = {
      stop_requested_event,
      samples_ready_event,
  };
  const auto wait_result = WaitForMultipleObjects(2, events, FALSE, timeout_ms);
  if (wait_result == WAIT_OBJECT_0) {
    return {WasapiEventWaitStatus::StopRequested};
  }
  if (wait_result == WAIT_OBJECT_0 + 1) {
    return {WasapiEventWaitStatus::SamplesReady};
  }
  if (wait_result == WAIT_TIMEOUT) {
    return {WasapiEventWaitStatus::TimedOut};
  }
  return {WasapiEventWaitStatus::Failed, GetLastError()};
}

}  // namespace

WasapiStreamResult WasapiStreamResult::success() {
  return WasapiStreamResult({});
}

WasapiStreamResult WasapiStreamResult::failure(std::vector<WasapiStreamError> errors) {
  return WasapiStreamResult(std::move(errors));
}

bool WasapiStreamResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<WasapiStreamError>& WasapiStreamResult::errors() const noexcept {
  return errors_;
}

WasapiStreamResult::WasapiStreamResult(std::vector<WasapiStreamError> errors)
    : errors_(std::move(errors)) {}

WasapiStreamIoResult WasapiStreamIoResult::success(std::uint32_t frames,
                                                   bool data_discontinuity,
                                                   bool timestamp_error) {
  const auto status = frames == 0 ? WasapiStreamIoStatus::Idle
                                  : WasapiStreamIoStatus::Completed;
  return {frames, status, false, data_discontinuity, timestamp_error, {}};
}

WasapiStreamIoResult WasapiStreamIoResult::success_silent(
    std::uint32_t frames,
    bool data_discontinuity,
    bool timestamp_error) {
  const auto status = frames == 0 ? WasapiStreamIoStatus::Idle
                                  : WasapiStreamIoStatus::Completed;
  return {frames, status, frames > 0, data_discontinuity, timestamp_error, {}};
}

WasapiStreamIoResult WasapiStreamIoResult::timeout() {
  return {0, WasapiStreamIoStatus::TimedOut, false, false, false, {}};
}

WasapiStreamIoResult WasapiStreamIoResult::cancellation() {
  return {0, WasapiStreamIoStatus::Cancelled, false, false, false, {}};
}

WasapiStreamIoResult WasapiStreamIoResult::failure(std::vector<WasapiStreamError> errors) {
  return {0, WasapiStreamIoStatus::Failed, false, false, false, std::move(errors)};
}

WasapiStreamIoResult WasapiStreamIoResult::failure(
    WasapiRealtimeErrorRecord error) noexcept {
  return {0, WasapiStreamIoStatus::Failed, false, false, false, {}, error};
}

bool WasapiStreamIoResult::ok() const noexcept {
  return status_ != WasapiStreamIoStatus::Failed;
}

std::uint32_t WasapiStreamIoResult::frames() const noexcept {
  return frames_;
}

WasapiStreamIoStatus WasapiStreamIoResult::status() const noexcept {
  return status_;
}

bool WasapiStreamIoResult::idle() const noexcept {
  return status_ == WasapiStreamIoStatus::Idle;
}

bool WasapiStreamIoResult::silent() const noexcept {
  return silent_;
}

bool WasapiStreamIoResult::data_discontinuity() const noexcept {
  return data_discontinuity_;
}

bool WasapiStreamIoResult::timestamp_error() const noexcept {
  return timestamp_error_;
}

bool WasapiStreamIoResult::timed_out() const noexcept {
  return status_ == WasapiStreamIoStatus::TimedOut;
}

bool WasapiStreamIoResult::cancelled() const noexcept {
  return status_ == WasapiStreamIoStatus::Cancelled;
}

const std::vector<WasapiStreamError>& WasapiStreamIoResult::errors() const noexcept {
  return errors_;
}

WasapiRealtimeErrorRecord WasapiStreamIoResult::realtime_error() const noexcept {
  return realtime_error_;
}

WasapiStreamIoResult::WasapiStreamIoResult(std::uint32_t frames,
                                           WasapiStreamIoStatus status,
                                           bool silent,
                                           bool data_discontinuity,
                                           bool timestamp_error,
                                           std::vector<WasapiStreamError> errors,
                                           WasapiRealtimeErrorRecord realtime_error)
    : frames_(frames),
      status_(status),
      silent_(silent),
      data_discontinuity_(data_discontinuity),
      timestamp_error_(timestamp_error),
      errors_(std::move(errors)),
      realtime_error_(realtime_error) {}

WindowsWasapiStream::WindowsWasapiStream() = default;

WindowsWasapiStream::WindowsWasapiStream(WindowsWasapiStream&& other) noexcept
    : state_(std::exchange(other.state_, WasapiStreamState::Closed)),
      probe_(std::move(other.probe_)),
      impl_(std::move(other.impl_)) {
  other.probe_ = {};
}

WindowsWasapiStream& WindowsWasapiStream::operator=(WindowsWasapiStream&& other) noexcept {
  if (this != &other) {
    close();
    state_ = std::exchange(other.state_, WasapiStreamState::Closed);
    probe_ = std::move(other.probe_);
    impl_ = std::move(other.impl_);
    other.probe_ = {};
  }
  return *this;
}

WindowsWasapiStream::~WindowsWasapiStream() = default;

WasapiStreamResult WindowsWasapiStream::open(WasapiStreamProbe probe) {
  if (state_ != WasapiStreamState::Closed) {
    return WasapiStreamResult::failure({
        {"stream_already_open", "WASAPI stream shell is already open."},
    });
  }

  auto errors = validate_probe(probe);
  if (!errors.empty()) {
    return WasapiStreamResult::failure(std::move(errors));
  }

  probe_ = std::move(probe);
  impl_.reset();
  state_ = WasapiStreamState::Open;
  return WasapiStreamResult::success();
}

WasapiStreamResult WindowsWasapiStream::start() noexcept {
  if (state_ == WasapiStreamState::Started) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Open) {
    return WasapiStreamResult::failure({
        {"stream_not_open", "WASAPI stream shell must be open before start."},
    });
  }

  if (impl_ && impl_->audio_client) {
    impl_->samples_ready_latched = false;
    if (impl_->stop_requested_event.valid()) {
      ResetEvent(impl_->stop_requested_event.get());
    }
    const auto start_result = impl_->audio_client->Start();
    if (FAILED(start_result)) {
      return WasapiStreamResult::failure({
          {
              "wasapi_start_failed",
              "WASAPI audio client start failed with " + hresult_hex(start_result) + ".",
              static_cast<std::int32_t>(start_result),
          },
      });
    }
  }

  state_ = WasapiStreamState::Started;
  return WasapiStreamResult::success();
}

WasapiStreamResult WindowsWasapiStream::stop() noexcept {
  if (state_ == WasapiStreamState::Open) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamResult::failure({
        {"stream_not_started", "WASAPI stream shell is not started."},
    });
  }

  const auto stop_result = impl_ && impl_->audio_client
                               ? impl_->audio_client->Stop()
                               : S_OK;
  return complete_stop(static_cast<std::int32_t>(stop_result));
}

WasapiStreamResult WindowsWasapiStream::complete_stop(
    std::int32_t stop_result) noexcept {
  state_ = WasapiStreamState::Open;
  const auto native_result = static_cast<HRESULT>(stop_result);
  if (FAILED(native_result)) {
    return WasapiStreamResult::failure({
        {
            "wasapi_stop_failed",
            "WASAPI audio client stop failed with " +
                hresult_hex(native_result) + ".",
            static_cast<std::int32_t>(native_result),
        },
    });
  }
  return WasapiStreamResult::success();
}

WasapiStreamIoResult WindowsWasapiStream::render_once(
    const realtime::AudioBuffer& source,
    std::uint32_t frames,
    std::uint32_t timeout_ms) noexcept {
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::StreamNotStarted,
                       WasapiRealtimeErrorContext::Render));
  }
  if (probe_.direction != WasapiStreamDirection::Render) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::WrongStreamDirection,
                       WasapiRealtimeErrorContext::Render));
  }
  if (!impl_ || !impl_->audio_client || !impl_->render_client ||
      !impl_->samples_ready_event.valid() || !impl_->stop_requested_event.valid()) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::NativeStreamUnavailable,
                       WasapiRealtimeErrorContext::Render));
  }

  if (!std::exchange(impl_->samples_ready_latched, false)) {
    const auto wait_result = wait_for_stream_event(impl_->samples_ready_event.get(),
                                                  impl_->stop_requested_event.get(),
                                                  timeout_ms);
    if (wait_result.status == WasapiEventWaitStatus::StopRequested) {
      return WasapiStreamIoResult::cancellation();
    }
    if (wait_result.status != WasapiEventWaitStatus::SamplesReady &&
        wait_result.status != WasapiEventWaitStatus::TimedOut) {
      return WasapiStreamIoResult::failure(realtime_error(
          WasapiRealtimeErrorCode::WasapiEventWaitFailed,
          static_cast<WasapiRealtimeErrorContext>(
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Render) |
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeWin32)),
          wait_result.native_win32_code));
    }
  }

  UINT32 padding_frames = 0;
  // The event timeout is a polling boundary; current padding is authoritative.
  const auto padding_result = impl_->audio_client->GetCurrentPadding(&padding_frames);
  if (FAILED(padding_result)) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiPaddingFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Render) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(padding_result)));
  }

  if (padding_frames >= probe_.buffer_frames) {
    return WasapiStreamIoResult::success(0);
  }

  const auto requested_frames = std::min<std::uint32_t>(
      {probe_.buffer_frames - padding_frames,
       static_cast<std::uint32_t>(source.frames()), frames});
  if (requested_frames == 0) {
    return WasapiStreamIoResult::success(0);
  }

  BYTE* render_buffer = nullptr;
  const auto get_buffer_result =
      impl_->render_client->GetBuffer(requested_frames, &render_buffer);
  if (FAILED(get_buffer_result) || render_buffer == nullptr) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiRenderBufferFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Render) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(get_buffer_result)));
  }

  const auto conversion = export_float_to_interleaved(source,
                                                     probe_.mix_format,
                                                     render_buffer,
                                                     required_interleaved_bytes(probe_.mix_format,
                                                                                requested_frames),
                                                     requested_frames);
  if (!conversion.ok()) {
    static_cast<void>(
        impl_->render_client->ReleaseBuffer(requested_frames, AUDCLNT_BUFFERFLAGS_SILENT));
    return WasapiStreamIoResult::failure(sample_conversion_error(conversion.status()));
  }

  const auto release_result = impl_->render_client->ReleaseBuffer(requested_frames, 0);
  if (FAILED(release_result)) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiRenderBufferReleaseFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Render) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(release_result)));
  }

  return WasapiStreamIoResult::success(requested_frames);
}

WasapiStreamIoResult WindowsWasapiStream::capture_once(
    realtime::AudioBuffer& destination,
    std::uint32_t timeout_ms) noexcept {
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::StreamNotStarted,
                       WasapiRealtimeErrorContext::Capture));
  }
  if (probe_.direction != WasapiStreamDirection::Capture) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::WrongStreamDirection,
                       WasapiRealtimeErrorContext::Capture));
  }
  if (!impl_ || !impl_->capture_client || !impl_->samples_ready_event.valid() ||
      !impl_->stop_requested_event.valid()) {
    return WasapiStreamIoResult::failure(
        realtime_error(WasapiRealtimeErrorCode::NativeStreamUnavailable,
                       WasapiRealtimeErrorContext::Capture));
  }

  const auto samples_ready_latched =
      std::exchange(impl_->samples_ready_latched, false);
  UINT32 packet_frames = 0;
  auto packet_result = impl_->capture_client->GetNextPacketSize(&packet_frames);
  if (FAILED(packet_result)) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiCapturePacketFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Capture) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(packet_result)));
  }

  if (packet_frames == 0 && !samples_ready_latched) {
    const auto wait_result = wait_for_stream_event(impl_->samples_ready_event.get(),
                                                  impl_->stop_requested_event.get(),
                                                  timeout_ms);
    if (wait_result.status == WasapiEventWaitStatus::StopRequested) {
      return WasapiStreamIoResult::cancellation();
    }
    if (wait_result.status == WasapiEventWaitStatus::TimedOut) {
      return WasapiStreamIoResult::timeout();
    }
    if (wait_result.status != WasapiEventWaitStatus::SamplesReady) {
      return WasapiStreamIoResult::failure(realtime_error(
          WasapiRealtimeErrorCode::WasapiEventWaitFailed,
          static_cast<WasapiRealtimeErrorContext>(
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Capture) |
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeWin32)),
          wait_result.native_win32_code));
    }

    packet_result = impl_->capture_client->GetNextPacketSize(&packet_frames);
    if (FAILED(packet_result)) {
      return WasapiStreamIoResult::failure(realtime_error(
          WasapiRealtimeErrorCode::WasapiCapturePacketFailed,
          static_cast<WasapiRealtimeErrorContext>(
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Capture) |
              static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
          static_cast<std::uint32_t>(packet_result)));
    }
  }
  if (packet_frames == 0) {
    return WasapiStreamIoResult::success(0);
  }
  if (packet_frames > destination.frames()) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::CaptureBufferTooSmall,
        WasapiRealtimeErrorContext::Capture));
  }

  BYTE* capture_buffer = nullptr;
  DWORD flags = 0;
  UINT64 device_position = 0;
  UINT64 qpc_position = 0;
  const auto get_buffer_result = impl_->capture_client->GetBuffer(
      &capture_buffer,
      &packet_frames,
      &flags,
      &device_position,
      &qpc_position);
  if (FAILED(get_buffer_result)) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiCaptureBufferFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Capture) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(get_buffer_result)));
  }

  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
    for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
      auto samples = destination.channel(channel);
      std::fill_n(samples.begin(), packet_frames, 0.0F);
    }
  } else {
    const auto conversion = import_interleaved_to_float(
        capture_buffer,
        required_interleaved_bytes(probe_.mix_format, packet_frames),
        probe_.mix_format,
        destination,
        packet_frames);
    if (!conversion.ok()) {
      static_cast<void>(impl_->capture_client->ReleaseBuffer(packet_frames));
      return WasapiStreamIoResult::failure(sample_conversion_error(conversion.status()));
    }
  }

  const auto release_result = impl_->capture_client->ReleaseBuffer(packet_frames);
  if (FAILED(release_result)) {
    return WasapiStreamIoResult::failure(realtime_error(
        WasapiRealtimeErrorCode::WasapiCaptureBufferReleaseFailed,
        static_cast<WasapiRealtimeErrorContext>(
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::Capture) |
            static_cast<std::uint16_t>(WasapiRealtimeErrorContext::NativeHresult)),
        static_cast<std::uint32_t>(release_result)));
  }

  const auto data_discontinuity =
      (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
  const auto timestamp_error =
      (flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0;
  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
    return WasapiStreamIoResult::success_silent(
        packet_frames, data_discontinuity, timestamp_error);
  }
  return WasapiStreamIoResult::success(
      packet_frames, data_discontinuity, timestamp_error);
}

WasapiDuplexEventWaitStatus wait_for_wasapi_duplex_events(
    WindowsWasapiStream& capture_stream,
    WindowsWasapiStream& render_stream,
    std::uint32_t timeout_ms) noexcept {
  if (capture_stream.state_ != WasapiStreamState::Started ||
      render_stream.state_ != WasapiStreamState::Started ||
      capture_stream.probe_.direction != WasapiStreamDirection::Capture ||
      render_stream.probe_.direction != WasapiStreamDirection::Render ||
      !capture_stream.impl_ || !render_stream.impl_ ||
      !capture_stream.impl_->samples_ready_event.valid() ||
      !capture_stream.impl_->stop_requested_event.valid() ||
      !render_stream.impl_->samples_ready_event.valid() ||
      !render_stream.impl_->stop_requested_event.valid()) {
    return WasapiDuplexEventWaitStatus::Unavailable;
  }

  HANDLE events[] = {
      capture_stream.impl_->stop_requested_event.get(),
      render_stream.impl_->stop_requested_event.get(),
      capture_stream.impl_->samples_ready_event.get(),
      render_stream.impl_->samples_ready_event.get(),
  };
  const auto readiness_pending =
      capture_stream.impl_->samples_ready_latched ||
      render_stream.impl_->samples_ready_latched;
  const auto wait_result = WaitForMultipleObjects(
      4, events, FALSE, readiness_pending ? 0U : timeout_ms);
  if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_OBJECT_0 + 1) {
    return WasapiDuplexEventWaitStatus::Cancelled;
  }
  if (wait_result == WAIT_OBJECT_0 + 2) {
    capture_stream.impl_->samples_ready_latched = true;
    return WasapiDuplexEventWaitStatus::Ready;
  }
  if (wait_result == WAIT_OBJECT_0 + 3) {
    render_stream.impl_->samples_ready_latched = true;
    return WasapiDuplexEventWaitStatus::Ready;
  }
  if (wait_result == WAIT_TIMEOUT) {
    return readiness_pending ? WasapiDuplexEventWaitStatus::Ready
                             : WasapiDuplexEventWaitStatus::TimedOut;
  }
  return WasapiDuplexEventWaitStatus::Failed;
}

bool WindowsWasapiStream::read_clock(WasapiClockSnapshot& snapshot) const noexcept {
  snapshot = {};
  if (!impl_ || !impl_->audio_clock || impl_->clock_frequency == 0) {
    return false;
  }

  UINT64 position = 0;
  UINT64 qpc_position = 0;
  if (FAILED(impl_->audio_clock->GetPosition(&position, &qpc_position))) {
    return false;
  }

  snapshot.position = position;
  snapshot.qpc_position_100ns = qpc_position;
  snapshot.frequency = impl_->clock_frequency;
  return true;
}

void WindowsWasapiStream::request_stop() noexcept {
  if (impl_ && impl_->stop_requested_event.valid()) {
    SetEvent(impl_->stop_requested_event.get());
  }
}

void WindowsWasapiStream::close() noexcept {
  if (state_ == WasapiStreamState::Started && impl_ && impl_->audio_client) {
    static_cast<void>(impl_->audio_client->Stop());
  }
  state_ = WasapiStreamState::Closed;
  probe_ = {};
  impl_.reset();
}

WasapiStreamState WindowsWasapiStream::state() const noexcept {
  return state_;
}

const WasapiStreamProbe& WindowsWasapiStream::probe() const noexcept {
  return probe_;
}

WasapiStreamDiagnostics WindowsWasapiStream::diagnostics() const noexcept {
  WasapiStreamDiagnostics diagnostics;
  diagnostics.state = state_;
  diagnostics.direction = probe_.direction;
  diagnostics.mode = probe_.mode;
  diagnostics.mix_format = probe_.mix_format;
  diagnostics.buffer_frames = probe_.buffer_frames;
  diagnostics.default_period_100ns = probe_.default_period_100ns;
  diagnostics.minimum_period_100ns = probe_.minimum_period_100ns;
  return diagnostics;
}

WasapiStreamOpenResult WasapiStreamOpenResult::success(WindowsWasapiStream stream) {
  return {std::move(stream), {}};
}

WasapiStreamOpenResult WasapiStreamOpenResult::failure(std::vector<WasapiStreamError> errors) {
  return {{}, std::move(errors)};
}

bool WasapiStreamOpenResult::ok() const noexcept {
  return errors_.empty();
}

WindowsWasapiStream& WasapiStreamOpenResult::stream() noexcept {
  return stream_;
}

const WindowsWasapiStream& WasapiStreamOpenResult::stream() const noexcept {
  return stream_;
}

WindowsWasapiStream WasapiStreamOpenResult::take_stream() noexcept {
  return std::move(stream_);
}

const std::vector<WasapiStreamError>& WasapiStreamOpenResult::errors() const noexcept {
  return errors_;
}

WasapiStreamOpenResult::WasapiStreamOpenResult(WindowsWasapiStream stream,
                                               std::vector<WasapiStreamError> errors)
    : stream_(std::move(stream)), errors_(std::move(errors)) {}

WasapiStreamOpenResult open_wasapi_stream_shell(
    WasapiStreamProbe probe,
    std::uint32_t requested_sample_rate,
    std::uint32_t requested_period_frames) {
  WindowsWasapiStream stream;
  auto open_result = stream.open(std::move(probe));
  if (!open_result.ok()) {
    return WasapiStreamOpenResult::failure(open_result.errors());
  }
  const auto direction = stream.probe().direction;
  const auto mode = stream.probe().mode;

  auto impl = std::make_unique<WindowsWasapiStream::Impl>();
  if (!impl->apartment.ok()) {
    return WasapiStreamOpenResult::failure({
        {
            "com_initialize_failed",
            "COM initialization failed with " + hresult_hex(impl->apartment.result()) + ".",
            static_cast<std::int32_t>(impl->apartment.result()),
        },
    });
  }

  const auto create_result = CoCreateInstance(__uuidof(MMDeviceEnumerator),
                                             nullptr,
                                             CLSCTX_ALL,
                                             __uuidof(IMMDeviceEnumerator),
                                             reinterpret_cast<void**>(impl->enumerator.put()));
  if (FAILED(create_result) || !impl->enumerator) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_enumerator_failed",
            "WASAPI enumerator creation failed with " + hresult_hex(create_result) + ".",
            static_cast<std::int32_t>(create_result),
        },
    });
  }

  const auto wide_device_id = utf8_to_wide(stream.probe().device_id);
  if (wide_device_id.empty()) {
    return WasapiStreamOpenResult::failure({
        {
            "invalid_device_id_encoding",
            "WASAPI device ID must be valid non-empty UTF-8.",
        },
    });
  }
  const auto endpoint_result =
      impl->enumerator->GetDevice(wide_device_id.c_str(), impl->device.put());
  if (FAILED(endpoint_result) || !impl->device) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_device_lookup_failed",
            "WASAPI device lookup failed with " + hresult_hex(endpoint_result) + ".",
            static_cast<std::int32_t>(endpoint_result),
        },
    });
  }

  const auto activate_result = impl->device->Activate(
      __uuidof(IAudioClient),
      CLSCTX_ALL,
      nullptr,
      reinterpret_cast<void**>(impl->audio_client.put()));
  if (FAILED(activate_result) || !impl->audio_client) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_audio_client_failed",
            "WASAPI audio client activation failed with " + hresult_hex(activate_result) + ".",
            static_cast<std::int32_t>(activate_result),
        },
    });
  }

  const auto format_result = impl->audio_client->GetMixFormat(&impl->wave_format);
  if (FAILED(format_result) || impl->wave_format == nullptr) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_mix_format_failed",
            "WASAPI mix format query failed with " + hresult_hex(format_result) + ".",
            static_cast<std::int32_t>(format_result),
        },
    });
  }
  if (impl->wave_format->nSamplesPerSec != stream.probe().mix_format.sample_rate ||
      impl->wave_format->nChannels != stream.probe().mix_format.channels ||
      impl->wave_format->wBitsPerSample !=
          stream.probe().mix_format.bits_per_sample) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_probe_format_changed",
            "WASAPI device mix format changed after it was probed.",
        },
    });
  }

  const bool use_audio_engine_resampler =
      requested_sample_rate != 0 &&
      requested_sample_rate != impl->wave_format->nSamplesPerSec;
  if (use_audio_engine_resampler) {
    if (mode != WasapiStreamMode::Endpoint) {
      return WasapiStreamOpenResult::failure({{
          "wasapi_resample_mode_unsupported",
          "WASAPI sample-rate conversion is only supported for endpoint streams.",
      }});
    }
    impl->wave_format->nSamplesPerSec = requested_sample_rate;
    impl->wave_format->nAvgBytesPerSec =
        requested_sample_rate * impl->wave_format->nBlockAlign;
  }

  const auto stream_flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            (use_audio_engine_resampler
                                 ? AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                       AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY
                                 : 0) |
                            (mode == WasapiStreamMode::Loopback
                                 ? AUDCLNT_STREAMFLAGS_LOOPBACK
                                 : 0);
  HRESULT init_result = E_NOINTERFACE;
  bool used_requested_period = false;
  if (requested_period_frames != 0 && !use_audio_engine_resampler &&
      mode == WasapiStreamMode::Endpoint &&
      SUCCEEDED(impl->audio_client->QueryInterface(
          __uuidof(IAudioClient3),
          reinterpret_cast<void**>(impl->audio_client3.put())))) {
    UINT32 default_period = 0;
    UINT32 fundamental_period = 0;
    UINT32 minimum_period = 0;
    UINT32 maximum_period = 0;
    if (SUCCEEDED(impl->audio_client3->GetSharedModeEnginePeriod(
            impl->wave_format, &default_period, &fundamental_period,
            &minimum_period, &maximum_period))) {
      const auto selected_period = select_wasapi_shared_period_frames(
          requested_period_frames, default_period, fundamental_period,
          minimum_period, maximum_period);
      if (selected_period != 0) {
        init_result = impl->audio_client3->InitializeSharedAudioStream(
            stream_flags, selected_period, impl->wave_format, nullptr);
        used_requested_period = SUCCEEDED(init_result);
      }
    }
  }
  if (!used_requested_period) {
    if (impl->audio_client3) {
      impl->audio_client3.reset();
      impl->audio_client.reset();
      init_result = impl->device->Activate(
          __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
          reinterpret_cast<void**>(impl->audio_client.put()));
    } else {
      init_result = S_OK;
    }
    if (SUCCEEDED(init_result) && impl->audio_client) {
      init_result = impl->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                   stream_flags,
                                                   0,
                                                   0,
                                                   impl->wave_format,
                                                   nullptr);
    }
  }
  if (FAILED(init_result)) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_initialize_failed",
            "WASAPI shared stream initialization failed with " + hresult_hex(init_result) + ".",
            static_cast<std::int32_t>(init_result),
        },
    });
  }

  UINT32 native_buffer_frames = 0;
  const auto buffer_size_result =
      impl->audio_client->GetBufferSize(&native_buffer_frames);
  if (FAILED(buffer_size_result) || native_buffer_frames == 0) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_buffer_size_failed",
            "WASAPI buffer size query failed with " +
                hresult_hex(buffer_size_result) + ".",
            static_cast<std::int32_t>(buffer_size_result),
        },
    });
  }
  if (!use_audio_engine_resampler && !used_requested_period &&
      native_buffer_frames != stream.probe().buffer_frames) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_probe_buffer_changed",
            "WASAPI device buffer size changed after it was probed.",
        },
    });
  }
  if (use_audio_engine_resampler || used_requested_period) {
    stream.probe_.mix_format.sample_rate = impl->wave_format->nSamplesPerSec;
    stream.probe_.mix_format.frames_per_block = native_buffer_frames;
    stream.probe_.buffer_frames = native_buffer_frames;
  }

  const auto clock_result = impl->audio_client->GetService(
      __uuidof(IAudioClock),
      reinterpret_cast<void**>(impl->audio_clock.put()));
  if (FAILED(clock_result) || !impl->audio_clock) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_clock_client_failed",
            "WASAPI audio clock query failed with " + hresult_hex(clock_result) + ".",
            static_cast<std::int32_t>(clock_result),
        },
    });
  }
  const auto frequency_result =
      impl->audio_clock->GetFrequency(&impl->clock_frequency);
  if (FAILED(frequency_result) || impl->clock_frequency == 0) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_clock_frequency_failed",
            "WASAPI audio clock frequency query failed with " +
                hresult_hex(frequency_result) + ".",
            static_cast<std::int32_t>(frequency_result),
        },
    });
  }

  impl->samples_ready_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!impl->samples_ready_event.valid()) {
    const auto win32_code = GetLastError();
    const auto error = HRESULT_FROM_WIN32(win32_code);
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_event_create_failed",
            "WASAPI samples-ready event creation failed with " + hresult_hex(error) + ".",
            static_cast<std::int32_t>(error),
            win32_code,
        },
    });
  }

  impl->stop_requested_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!impl->stop_requested_event.valid()) {
    const auto win32_code = GetLastError();
    const auto error = HRESULT_FROM_WIN32(win32_code);
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_stop_event_create_failed",
            "WASAPI stop event creation failed with " + hresult_hex(error) + ".",
            static_cast<std::int32_t>(error),
            win32_code,
        },
    });
  }

  const auto event_result =
      impl->audio_client->SetEventHandle(impl->samples_ready_event.get());
  if (FAILED(event_result)) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_event_handle_failed",
            "WASAPI event handle registration failed with " + hresult_hex(event_result) + ".",
            static_cast<std::int32_t>(event_result),
        },
    });
  }

  if (direction == WasapiStreamDirection::Render) {
    const auto render_client_result = impl->audio_client->GetService(
        __uuidof(IAudioRenderClient),
        reinterpret_cast<void**>(impl->render_client.put()));
    if (FAILED(render_client_result) || !impl->render_client) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_client_failed",
              "WASAPI render client query failed with " + hresult_hex(render_client_result) + ".",
              static_cast<std::int32_t>(render_client_result),
          },
      });
    }

    BYTE* render_buffer = nullptr;
    const auto get_buffer_result =
        impl->render_client->GetBuffer(native_buffer_frames, &render_buffer);
    if (FAILED(get_buffer_result) || render_buffer == nullptr) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_buffer_failed",
              "WASAPI render buffer acquisition failed with " + hresult_hex(get_buffer_result) + ".",
              static_cast<std::int32_t>(get_buffer_result),
          },
      });
    }

    const auto release_result =
        impl->render_client->ReleaseBuffer(native_buffer_frames,
                                           AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(release_result)) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_buffer_release_failed",
              "WASAPI silent render buffer release failed with " + hresult_hex(release_result) + ".",
              static_cast<std::int32_t>(release_result),
          },
      });
    }
  } else {
    const auto capture_client_result = impl->audio_client->GetService(
        __uuidof(IAudioCaptureClient),
        reinterpret_cast<void**>(impl->capture_client.put()));
    if (FAILED(capture_client_result) || !impl->capture_client) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_capture_client_failed",
              "WASAPI capture client query failed with " + hresult_hex(capture_client_result) + ".",
              static_cast<std::int32_t>(capture_client_result),
          },
      });
    }
  }

  stream.impl_ = std::move(impl);

  return WasapiStreamOpenResult::success(std::move(stream));
}

WasapiStreamOpenResult open_default_wasapi_stream_shell(WasapiStreamDirection direction,
                                                        WasapiStreamMode mode,
                                                        std::uint32_t requested_sample_rate,
                                                        std::uint32_t requested_period_frames) {
  auto probe_result = probe_default_wasapi_stream(direction, mode);
  if (!probe_result.ok()) {
    return WasapiStreamOpenResult::failure(convert_probe_errors(probe_result.errors()));
  }
  return open_wasapi_stream_shell(probe_result.probe(), requested_sample_rate,
                                  requested_period_frames);
}

}  // namespace sar::platform
