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
#include <memory>
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

EDataFlow data_flow(WasapiStreamDirection direction) noexcept {
  return direction == WasapiStreamDirection::Render ? eRender : eCapture;
}

std::vector<WasapiStreamError> validate_probe(const WasapiStreamProbe& probe) {
  std::vector<WasapiStreamError> errors;

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

WasapiStreamError sample_conversion_error(SampleConversionStatus status) {
  switch (status) {
    case SampleConversionStatus::Ok:
      return {"sample_conversion_ok", "Sample conversion succeeded."};
    case SampleConversionStatus::UnsupportedFormat:
      return {"unsupported_sample_format", "WASAPI stream sample format is not supported yet."};
    case SampleConversionStatus::BufferTooSmall:
      return {"sample_buffer_too_small", "Audio buffer is too small for the requested frames."};
    case SampleConversionStatus::ChannelMismatch:
      return {"sample_channel_mismatch", "Audio buffer channel count does not match the stream."};
  }
  return {"sample_conversion_failed", "Sample conversion failed."};
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
  ComPtr<IAudioRenderClient> render_client;
  ComPtr<IAudioCaptureClient> capture_client;
  UniqueHandle samples_ready_event;
  UniqueHandle stop_requested_event;
  WAVEFORMATEX* wave_format = nullptr;

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

WasapiEventWaitStatus wait_for_stream_event(HANDLE samples_ready_event,
                                            HANDLE stop_requested_event,
                                            std::uint32_t timeout_ms) noexcept {
  HANDLE events[] = {
      samples_ready_event,
      stop_requested_event,
  };
  const auto wait_result = WaitForMultipleObjects(2, events, FALSE, timeout_ms);
  if (wait_result == WAIT_OBJECT_0) {
    return WasapiEventWaitStatus::SamplesReady;
  }
  if (wait_result == WAIT_OBJECT_0 + 1) {
    return WasapiEventWaitStatus::StopRequested;
  }
  if (wait_result == WAIT_TIMEOUT) {
    return WasapiEventWaitStatus::TimedOut;
  }
  return WasapiEventWaitStatus::Failed;
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

WasapiStreamIoResult WasapiStreamIoResult::failure(std::vector<WasapiStreamError> errors) {
  return {0, WasapiStreamIoStatus::Failed, false, false, false, std::move(errors)};
}

bool WasapiStreamIoResult::ok() const noexcept {
  return errors_.empty();
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

const std::vector<WasapiStreamError>& WasapiStreamIoResult::errors() const noexcept {
  return errors_;
}

WasapiStreamIoResult::WasapiStreamIoResult(std::uint32_t frames,
                                           WasapiStreamIoStatus status,
                                           bool silent,
                                           bool data_discontinuity,
                                           bool timestamp_error,
                                           std::vector<WasapiStreamError> errors)
    : frames_(frames),
      status_(status),
      silent_(silent),
      data_discontinuity_(data_discontinuity),
      timestamp_error_(timestamp_error),
      errors_(std::move(errors)) {}

WindowsWasapiStream::WindowsWasapiStream() = default;

WindowsWasapiStream::WindowsWasapiStream(WindowsWasapiStream&&) noexcept = default;

WindowsWasapiStream& WindowsWasapiStream::operator=(WindowsWasapiStream&&) noexcept = default;

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

WasapiStreamResult WindowsWasapiStream::start() {
  if (state_ == WasapiStreamState::Started) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Open) {
    return WasapiStreamResult::failure({
        {"stream_not_open", "WASAPI stream shell must be open before start."},
    });
  }

  if (impl_ && impl_->audio_client) {
    if (impl_->stop_requested_event.valid()) {
      ResetEvent(impl_->stop_requested_event.get());
    }
    const auto start_result = impl_->audio_client->Start();
    if (FAILED(start_result)) {
      return WasapiStreamResult::failure({
          {
              "wasapi_start_failed",
              "WASAPI audio client start failed with " + hresult_hex(start_result) + ".",
          },
      });
    }
  }

  state_ = WasapiStreamState::Started;
  return WasapiStreamResult::success();
}

WasapiStreamResult WindowsWasapiStream::stop() {
  if (state_ == WasapiStreamState::Open) {
    return WasapiStreamResult::success();
  }
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamResult::failure({
        {"stream_not_started", "WASAPI stream shell is not started."},
    });
  }

  if (impl_ && impl_->audio_client) {
    const auto stop_result = impl_->audio_client->Stop();
    if (FAILED(stop_result)) {
      return WasapiStreamResult::failure({
          {
              "wasapi_stop_failed",
              "WASAPI audio client stop failed with " + hresult_hex(stop_result) + ".",
          },
      });
    }
  }

  state_ = WasapiStreamState::Open;
  return WasapiStreamResult::success();
}

WasapiStreamIoResult WindowsWasapiStream::render_once(
    const realtime::AudioBuffer& source,
    std::uint32_t timeout_ms) noexcept {
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamIoResult::failure({
        {"stream_not_started", "WASAPI render requires a started stream."},
    });
  }
  if (probe_.direction != WasapiStreamDirection::Render) {
    return WasapiStreamIoResult::failure({
        {"wrong_stream_direction", "WASAPI render requires a render stream."},
    });
  }
  if (!impl_ || !impl_->audio_client || !impl_->render_client ||
      !impl_->samples_ready_event.valid() || !impl_->stop_requested_event.valid()) {
    return WasapiStreamIoResult::failure({
        {"native_stream_unavailable", "WASAPI render requires a native opened stream."},
    });
  }

  const auto wait_result = wait_for_stream_event(impl_->samples_ready_event.get(),
                                                impl_->stop_requested_event.get(),
                                                timeout_ms);
  if (wait_result == WasapiEventWaitStatus::StopRequested) {
    return WasapiStreamIoResult::success(0);
  }
  if (wait_result == WasapiEventWaitStatus::TimedOut) {
    return WasapiStreamIoResult::timeout();
  }
  if (wait_result != WasapiEventWaitStatus::SamplesReady) {
    return WasapiStreamIoResult::failure({
        {"wasapi_event_wait_failed", "WASAPI render event wait failed."},
    });
  }

  UINT32 padding_frames = 0;
  const auto padding_result = impl_->audio_client->GetCurrentPadding(&padding_frames);
  if (FAILED(padding_result)) {
    return WasapiStreamIoResult::failure({
        {
            "wasapi_padding_failed",
            "WASAPI render padding query failed with " + hresult_hex(padding_result) + ".",
        },
    });
  }

  if (padding_frames >= probe_.buffer_frames) {
    return WasapiStreamIoResult::success(0);
  }

  const auto requested_frames = std::min<std::uint32_t>(
      probe_.buffer_frames - padding_frames,
      static_cast<std::uint32_t>(source.frames()));
  if (requested_frames == 0) {
    return WasapiStreamIoResult::success(0);
  }

  BYTE* render_buffer = nullptr;
  const auto get_buffer_result =
      impl_->render_client->GetBuffer(requested_frames, &render_buffer);
  if (FAILED(get_buffer_result) || render_buffer == nullptr) {
    return WasapiStreamIoResult::failure({
        {
            "wasapi_render_buffer_failed",
            "WASAPI render buffer acquisition failed with " + hresult_hex(get_buffer_result) + ".",
        },
    });
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
    return WasapiStreamIoResult::failure({sample_conversion_error(conversion.status())});
  }

  const auto release_result = impl_->render_client->ReleaseBuffer(requested_frames, 0);
  if (FAILED(release_result)) {
    return WasapiStreamIoResult::failure({
        {
            "wasapi_render_buffer_release_failed",
            "WASAPI render buffer release failed with " + hresult_hex(release_result) + ".",
        },
    });
  }

  return WasapiStreamIoResult::success(requested_frames);
}

WasapiStreamIoResult WindowsWasapiStream::capture_once(
    realtime::AudioBuffer& destination,
    std::uint32_t timeout_ms) noexcept {
  if (state_ != WasapiStreamState::Started) {
    return WasapiStreamIoResult::failure({
        {"stream_not_started", "WASAPI capture requires a started stream."},
    });
  }
  if (probe_.direction != WasapiStreamDirection::Capture) {
    return WasapiStreamIoResult::failure({
        {"wrong_stream_direction", "WASAPI capture requires a capture stream."},
    });
  }
  if (!impl_ || !impl_->capture_client || !impl_->samples_ready_event.valid() ||
      !impl_->stop_requested_event.valid()) {
    return WasapiStreamIoResult::failure({
        {"native_stream_unavailable", "WASAPI capture requires a native opened stream."},
    });
  }

  const auto wait_result = wait_for_stream_event(impl_->samples_ready_event.get(),
                                                impl_->stop_requested_event.get(),
                                                timeout_ms);
  if (wait_result == WasapiEventWaitStatus::StopRequested) {
    return WasapiStreamIoResult::success(0);
  }
  if (wait_result == WasapiEventWaitStatus::TimedOut) {
    return WasapiStreamIoResult::timeout();
  }
  if (wait_result != WasapiEventWaitStatus::SamplesReady) {
    return WasapiStreamIoResult::failure({
        {"wasapi_event_wait_failed", "WASAPI capture event wait failed."},
    });
  }

  UINT32 packet_frames = 0;
  const auto packet_result = impl_->capture_client->GetNextPacketSize(&packet_frames);
  if (FAILED(packet_result)) {
    return WasapiStreamIoResult::failure({
        {
            "wasapi_capture_packet_failed",
            "WASAPI capture packet query failed with " + hresult_hex(packet_result) + ".",
        },
    });
  }
  if (packet_frames == 0) {
    return WasapiStreamIoResult::success(0);
  }
  if (packet_frames > destination.frames()) {
    return WasapiStreamIoResult::failure({
        {"capture_buffer_too_small", "Capture destination buffer cannot hold the packet."},
    });
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
    return WasapiStreamIoResult::failure({
        {
            "wasapi_capture_buffer_failed",
            "WASAPI capture buffer acquisition failed with " + hresult_hex(get_buffer_result) + ".",
        },
    });
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
      return WasapiStreamIoResult::failure({sample_conversion_error(conversion.status())});
    }
  }

  const auto release_result = impl_->capture_client->ReleaseBuffer(packet_frames);
  if (FAILED(release_result)) {
    return WasapiStreamIoResult::failure({
        {
            "wasapi_capture_buffer_release_failed",
            "WASAPI capture buffer release failed with " + hresult_hex(release_result) + ".",
        },
    });
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

WasapiStreamOpenResult open_default_wasapi_stream_shell(WasapiStreamDirection direction) {
  auto probe_result = probe_default_wasapi_stream(direction);
  if (!probe_result.ok()) {
    return WasapiStreamOpenResult::failure(convert_probe_errors(probe_result.errors()));
  }

  WindowsWasapiStream stream;
  auto open_result = stream.open(probe_result.probe());
  if (!open_result.ok()) {
    return WasapiStreamOpenResult::failure(open_result.errors());
  }

  auto impl = std::make_unique<WindowsWasapiStream::Impl>();
  if (!impl->apartment.ok()) {
    return WasapiStreamOpenResult::failure({
        {
            "com_initialize_failed",
            "COM initialization failed with " + hresult_hex(impl->apartment.result()) + ".",
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
        },
    });
  }

  const auto endpoint_result = impl->enumerator->GetDefaultAudioEndpoint(
      data_flow(direction),
      eConsole,
      impl->device.put());
  if (FAILED(endpoint_result) || !impl->device) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_default_endpoint_failed",
            "WASAPI default endpoint query failed with " + hresult_hex(endpoint_result) + ".",
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
        },
    });
  }

  const auto format_result = impl->audio_client->GetMixFormat(&impl->wave_format);
  if (FAILED(format_result) || impl->wave_format == nullptr) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_mix_format_failed",
            "WASAPI mix format query failed with " + hresult_hex(format_result) + ".",
        },
    });
  }

  const auto init_result = impl->audio_client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                                         AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                                         0,
                                                         0,
                                                         impl->wave_format,
                                                         nullptr);
  if (FAILED(init_result)) {
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_initialize_failed",
            "WASAPI shared stream initialization failed with " + hresult_hex(init_result) + ".",
        },
    });
  }

  impl->samples_ready_event.reset(CreateEventW(nullptr, FALSE, FALSE, nullptr));
  if (!impl->samples_ready_event.valid()) {
    const auto error = HRESULT_FROM_WIN32(GetLastError());
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_event_create_failed",
            "WASAPI samples-ready event creation failed with " + hresult_hex(error) + ".",
        },
    });
  }

  impl->stop_requested_event.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
  if (!impl->stop_requested_event.valid()) {
    const auto error = HRESULT_FROM_WIN32(GetLastError());
    return WasapiStreamOpenResult::failure({
        {
            "wasapi_stop_event_create_failed",
            "WASAPI stop event creation failed with " + hresult_hex(error) + ".",
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
          },
      });
    }

    UINT32 render_buffer_frames = 0;
    const auto buffer_size_result = impl->audio_client->GetBufferSize(&render_buffer_frames);
    if (FAILED(buffer_size_result) || render_buffer_frames == 0) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_buffer_size_failed",
              "WASAPI render buffer size query failed with " + hresult_hex(buffer_size_result) + ".",
          },
      });
    }

    BYTE* render_buffer = nullptr;
    const auto get_buffer_result =
        impl->render_client->GetBuffer(render_buffer_frames, &render_buffer);
    if (FAILED(get_buffer_result) || render_buffer == nullptr) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_buffer_failed",
              "WASAPI render buffer acquisition failed with " + hresult_hex(get_buffer_result) + ".",
          },
      });
    }

    const auto release_result =
        impl->render_client->ReleaseBuffer(render_buffer_frames, AUDCLNT_BUFFERFLAGS_SILENT);
    if (FAILED(release_result)) {
      return WasapiStreamOpenResult::failure({
          {
              "wasapi_render_buffer_release_failed",
              "WASAPI silent render buffer release failed with " + hresult_hex(release_result) + ".",
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
          },
      });
    }
  }

  stream.impl_ = std::move(impl);

  return WasapiStreamOpenResult::success(std::move(stream));
}

}  // namespace sar::platform
