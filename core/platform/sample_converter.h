#pragma once

#include "core/platform/audio_device.h"
#include "core/realtime/audio_buffer.h"

#include <cstddef>
#include <cstdint>

namespace sar::platform {

enum class SampleConversionStatus {
  Ok,
  UnsupportedFormat,
  BufferTooSmall,
  ChannelMismatch,
};

class SampleConversionResult {
 public:
  static SampleConversionResult success();
  static SampleConversionResult failure(SampleConversionStatus status);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] SampleConversionStatus status() const noexcept;

 private:
  explicit SampleConversionResult(SampleConversionStatus status);

  SampleConversionStatus status_ = SampleConversionStatus::Ok;
};

[[nodiscard]] std::size_t required_interleaved_bytes(const AudioFormat& format,
                                                     std::size_t frames) noexcept;

[[nodiscard]] SampleConversionResult import_interleaved_to_float(
    const void* source,
    std::size_t source_bytes,
    const AudioFormat& source_format,
    realtime::AudioBuffer& destination) noexcept;

[[nodiscard]] SampleConversionResult export_float_to_interleaved(
    const realtime::AudioBuffer& source,
    const AudioFormat& destination_format,
    void* destination,
    std::size_t destination_bytes) noexcept;

}  // namespace sar::platform
