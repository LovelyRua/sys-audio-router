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

struct WasapiStreamProbe {
  WasapiStreamDirection direction = WasapiStreamDirection::Render;
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
    WasapiStreamDirection direction);

}  // namespace sar::platform
