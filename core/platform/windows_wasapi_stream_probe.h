#pragma once

#include "core/platform/audio_device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

enum class WasapiStreamDirection {
  Render,
  Capture,
};

enum class WasapiStreamMode {
  Endpoint,
  Loopback,
};

[[nodiscard]] const char* wasapi_stream_direction_name(
    WasapiStreamDirection direction) noexcept;
[[nodiscard]] const char* wasapi_stream_mode_name(
    WasapiStreamMode mode) noexcept;

struct WasapiStreamProbe {
  WasapiStreamDirection direction = WasapiStreamDirection::Render;
  WasapiStreamMode mode = WasapiStreamMode::Endpoint;
  std::string device_id;
  std::string device_label;
  AudioFormat mix_format;
  std::uint64_t default_period_100ns = 0;
  std::uint64_t minimum_period_100ns = 0;
  std::uint32_t buffer_frames = 0;
};

struct WasapiStreamProbeError {
  std::string code;
  std::string message;
};

class WasapiStreamProbeResult {
 public:
  static WasapiStreamProbeResult success(WasapiStreamProbe probe);
  static WasapiStreamProbeResult failure(std::vector<WasapiStreamProbeError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const WasapiStreamProbe& probe() const noexcept;
  [[nodiscard]] const std::vector<WasapiStreamProbeError>& errors() const noexcept;

 private:
  WasapiStreamProbeResult(WasapiStreamProbe probe,
                          std::vector<WasapiStreamProbeError> errors);

  WasapiStreamProbe probe_;
  std::vector<WasapiStreamProbeError> errors_;
};

[[nodiscard]] WasapiStreamProbeResult probe_default_wasapi_stream(
    WasapiStreamDirection direction,
    WasapiStreamMode mode = WasapiStreamMode::Endpoint);

[[nodiscard]] WasapiStreamProbeResult probe_wasapi_stream(
    const std::string& device_id,
    WasapiStreamDirection direction,
    WasapiStreamMode mode = WasapiStreamMode::Endpoint);

}  // namespace sar::platform
