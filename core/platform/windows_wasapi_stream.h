#pragma once

#include "core/realtime/audio_buffer.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sar::platform {

enum class WasapiStreamState {
  Closed,
  Open,
  Started,
};

[[nodiscard]] const char* wasapi_stream_state_name(
    WasapiStreamState state) noexcept;

struct WasapiStreamError {
  std::string code;
  std::string message;
};

struct WasapiStreamDiagnostics {
  WasapiStreamState state = WasapiStreamState::Closed;
  WasapiStreamDirection direction = WasapiStreamDirection::Render;
  WasapiStreamMode mode = WasapiStreamMode::Endpoint;
  AudioFormat mix_format;
  std::uint32_t buffer_frames = 0;
  std::uint64_t default_period_100ns = 0;
  std::uint64_t minimum_period_100ns = 0;
};

struct WasapiClockSnapshot {
  std::uint64_t position = 0;
  std::uint64_t qpc_position_100ns = 0;
  std::uint64_t frequency = 0;
};

class WasapiStreamResult {
 public:
  static WasapiStreamResult success();
  static WasapiStreamResult failure(std::vector<WasapiStreamError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WasapiStreamError>& errors() const noexcept;

 private:
  explicit WasapiStreamResult(std::vector<WasapiStreamError> errors);

  std::vector<WasapiStreamError> errors_;
};

class WasapiStreamOpenResult;

enum class WasapiStreamIoStatus {
  Completed,
  Idle,
  TimedOut,
  Cancelled,
  Failed,
};

enum class WasapiDuplexEventWaitStatus {
  Ready,
  TimedOut,
  Cancelled,
  Failed,
  Unavailable,
};

[[nodiscard]] const char* wasapi_stream_io_status_name(
    WasapiStreamIoStatus status) noexcept;

class WasapiStreamIoResult {
 public:
  static WasapiStreamIoResult success(std::uint32_t frames,
                                      bool data_discontinuity = false,
                                      bool timestamp_error = false);
  static WasapiStreamIoResult success_silent(std::uint32_t frames,
                                             bool data_discontinuity = false,
                                             bool timestamp_error = false);
  static WasapiStreamIoResult timeout();
  static WasapiStreamIoResult cancellation();
  static WasapiStreamIoResult failure(std::vector<WasapiStreamError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::uint32_t frames() const noexcept;
  [[nodiscard]] WasapiStreamIoStatus status() const noexcept;
  [[nodiscard]] bool idle() const noexcept;
  [[nodiscard]] bool silent() const noexcept;
  [[nodiscard]] bool data_discontinuity() const noexcept;
  [[nodiscard]] bool timestamp_error() const noexcept;
  [[nodiscard]] bool timed_out() const noexcept;
  [[nodiscard]] bool cancelled() const noexcept;
  [[nodiscard]] const std::vector<WasapiStreamError>& errors() const noexcept;

 private:
  WasapiStreamIoResult(std::uint32_t frames,
                       WasapiStreamIoStatus status,
                       bool silent,
                       bool data_discontinuity,
                       bool timestamp_error,
                       std::vector<WasapiStreamError> errors);

  std::uint32_t frames_ = 0;
  WasapiStreamIoStatus status_ = WasapiStreamIoStatus::Idle;
  bool silent_ = false;
  bool data_discontinuity_ = false;
  bool timestamp_error_ = false;
  std::vector<WasapiStreamError> errors_;
};

class WasapiStreamIo {
 public:
  virtual ~WasapiStreamIo() = default;

  [[nodiscard]] virtual WasapiStreamResult start() noexcept = 0;
  [[nodiscard]] virtual WasapiStreamResult stop() noexcept = 0;
  [[nodiscard]] virtual WasapiStreamIoResult render_once(
      const realtime::AudioBuffer& source,
      std::uint32_t frames,
      std::uint32_t timeout_ms) noexcept = 0;
  [[nodiscard]] virtual WasapiStreamIoResult capture_once(
      realtime::AudioBuffer& destination,
      std::uint32_t timeout_ms) noexcept = 0;
  virtual void request_stop() noexcept = 0;
  [[nodiscard]] virtual const WasapiStreamProbe& probe() const noexcept = 0;
};

class WindowsWasapiStream final : public WasapiStreamIo {
 public:
  WindowsWasapiStream();
  WindowsWasapiStream(WindowsWasapiStream&&) noexcept;
  WindowsWasapiStream& operator=(WindowsWasapiStream&&) noexcept;
  WindowsWasapiStream(const WindowsWasapiStream&) = delete;
  WindowsWasapiStream& operator=(const WindowsWasapiStream&) = delete;
  ~WindowsWasapiStream() override;

  [[nodiscard]] WasapiStreamResult open(WasapiStreamProbe probe);
  [[nodiscard]] WasapiStreamResult start() noexcept override;
  [[nodiscard]] WasapiStreamResult stop() noexcept override;
  [[nodiscard]] WasapiStreamIoResult render_once(
      const realtime::AudioBuffer& source,
      std::uint32_t frames,
      std::uint32_t timeout_ms) noexcept override;
  [[nodiscard]] WasapiStreamIoResult capture_once(
      realtime::AudioBuffer& destination,
      std::uint32_t timeout_ms) noexcept override;
  [[nodiscard]] bool read_clock(WasapiClockSnapshot& snapshot) const noexcept;
  void request_stop() noexcept override;
  void close() noexcept;

  [[nodiscard]] WasapiStreamState state() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept override;
  [[nodiscard]] WasapiStreamDiagnostics diagnostics() const noexcept;

 private:
  friend WasapiDuplexEventWaitStatus wait_for_wasapi_duplex_events(
      WindowsWasapiStream& capture_stream,
      WindowsWasapiStream& render_stream,
      std::uint32_t timeout_ms) noexcept;
  friend WasapiStreamOpenResult open_wasapi_stream_shell(
      WasapiStreamProbe probe,
      std::uint32_t requested_sample_rate);
  friend WasapiStreamOpenResult open_default_wasapi_stream_shell(
      WasapiStreamDirection direction,
      WasapiStreamMode mode,
      std::uint32_t requested_sample_rate);

  struct Impl;

  WasapiStreamState state_ = WasapiStreamState::Closed;
  WasapiStreamProbe probe_;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] WasapiDuplexEventWaitStatus wait_for_wasapi_duplex_events(
    WindowsWasapiStream& capture_stream,
    WindowsWasapiStream& render_stream,
    std::uint32_t timeout_ms) noexcept;

class WasapiStreamOpenResult {
 public:
  static WasapiStreamOpenResult success(WindowsWasapiStream stream);
  static WasapiStreamOpenResult failure(std::vector<WasapiStreamError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsWasapiStream& stream() noexcept;
  [[nodiscard]] const WindowsWasapiStream& stream() const noexcept;
  [[nodiscard]] WindowsWasapiStream take_stream() noexcept;
  [[nodiscard]] const std::vector<WasapiStreamError>& errors() const noexcept;

 private:
  WasapiStreamOpenResult(WindowsWasapiStream stream,
                         std::vector<WasapiStreamError> errors);

  WindowsWasapiStream stream_;
  std::vector<WasapiStreamError> errors_;
};

[[nodiscard]] WasapiStreamOpenResult open_default_wasapi_stream_shell(
    WasapiStreamDirection direction,
    WasapiStreamMode mode = WasapiStreamMode::Endpoint,
    std::uint32_t requested_sample_rate = 0);

[[nodiscard]] WasapiStreamOpenResult open_wasapi_stream_shell(
    WasapiStreamProbe probe,
    std::uint32_t requested_sample_rate = 0);

}  // namespace sar::platform
