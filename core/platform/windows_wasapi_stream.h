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

struct WasapiStreamError {
  std::string code;
  std::string message;
};

struct WasapiStreamDiagnostics {
  WasapiStreamState state = WasapiStreamState::Closed;
  WasapiStreamDirection direction = WasapiStreamDirection::Render;
  AudioFormat mix_format;
  std::uint32_t buffer_frames = 0;
  std::uint64_t default_period_100ns = 0;
  std::uint64_t minimum_period_100ns = 0;
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
  Failed,
};

class WasapiStreamIoResult {
 public:
  static WasapiStreamIoResult success(std::uint32_t frames);
  static WasapiStreamIoResult timeout();
  static WasapiStreamIoResult failure(std::vector<WasapiStreamError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::uint32_t frames() const noexcept;
  [[nodiscard]] WasapiStreamIoStatus status() const noexcept;
  [[nodiscard]] bool idle() const noexcept;
  [[nodiscard]] bool timed_out() const noexcept;
  [[nodiscard]] const std::vector<WasapiStreamError>& errors() const noexcept;

 private:
  WasapiStreamIoResult(std::uint32_t frames,
                       WasapiStreamIoStatus status,
                       std::vector<WasapiStreamError> errors);

  std::uint32_t frames_ = 0;
  WasapiStreamIoStatus status_ = WasapiStreamIoStatus::Idle;
  std::vector<WasapiStreamError> errors_;
};

class WindowsWasapiStream {
 public:
  WindowsWasapiStream();
  WindowsWasapiStream(WindowsWasapiStream&&) noexcept;
  WindowsWasapiStream& operator=(WindowsWasapiStream&&) noexcept;
  WindowsWasapiStream(const WindowsWasapiStream&) = delete;
  WindowsWasapiStream& operator=(const WindowsWasapiStream&) = delete;
  ~WindowsWasapiStream();

  [[nodiscard]] WasapiStreamResult open(WasapiStreamProbe probe);
  [[nodiscard]] WasapiStreamResult start();
  [[nodiscard]] WasapiStreamResult stop();
  [[nodiscard]] WasapiStreamIoResult render_once(
      const realtime::AudioBuffer& source,
      std::uint32_t timeout_ms) noexcept;
  [[nodiscard]] WasapiStreamIoResult capture_once(
      realtime::AudioBuffer& destination,
      std::uint32_t timeout_ms) noexcept;
  void close() noexcept;

  [[nodiscard]] WasapiStreamState state() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept;
  [[nodiscard]] WasapiStreamDiagnostics diagnostics() const noexcept;

 private:
  friend WasapiStreamOpenResult open_default_wasapi_stream_shell(
      WasapiStreamDirection direction);

  struct Impl;

  WasapiStreamState state_ = WasapiStreamState::Closed;
  WasapiStreamProbe probe_;
  std::unique_ptr<Impl> impl_;
};

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
    WasapiStreamDirection direction);

}  // namespace sar::platform
