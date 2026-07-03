#pragma once

#include "core/platform/windows_wasapi_stream_probe.h"

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

class WindowsWasapiStream {
 public:
  [[nodiscard]] WasapiStreamResult open(WasapiStreamProbe probe);
  [[nodiscard]] WasapiStreamResult start();
  [[nodiscard]] WasapiStreamResult stop();
  void close() noexcept;

  [[nodiscard]] WasapiStreamState state() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept;

 private:
  WasapiStreamState state_ = WasapiStreamState::Closed;
  WasapiStreamProbe probe_;
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
