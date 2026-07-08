#include "core/platform/sample_converter.h"

#include <algorithm>
#include <cmath>

namespace sar::platform {

namespace {

std::size_t bytes_per_sample(const AudioFormat& format) noexcept {
  if (format.bits_per_sample % 8 != 0) {
    return 0;
  }
  return format.bits_per_sample / 8;
}

bool is_supported(const AudioFormat& format) noexcept {
  if (format.sample_format == AudioSampleFormat::IeeeFloat &&
      format.bits_per_sample == 32) {
    return true;
  }
  if (format.sample_format == AudioSampleFormat::PcmInt &&
      format.bits_per_sample == 24) {
    return true;
  }
  if (format.sample_format == AudioSampleFormat::PcmInt &&
      format.bits_per_sample == 16) {
    return true;
  }
  if (format.sample_format == AudioSampleFormat::PcmInt &&
      format.bits_per_sample == 32) {
    return true;
  }
  return false;
}

float int16_to_float(std::int16_t value) noexcept {
  return static_cast<float>(value) / 32768.0F;
}

std::int16_t float_to_int16(float value) noexcept {
  const auto clamped = std::clamp(value, -1.0F, 1.0F);
  return static_cast<std::int16_t>(std::lround(clamped * 32767.0F));
}

std::int32_t read_int24_le(const std::uint8_t* bytes) noexcept {
  auto value = static_cast<std::int32_t>(bytes[0]) |
               (static_cast<std::int32_t>(bytes[1]) << 8) |
               (static_cast<std::int32_t>(bytes[2]) << 16);
  if ((value & 0x00800000) != 0) {
    value |= static_cast<std::int32_t>(0xFF000000);
  }
  return value;
}

float int24_to_float(std::int32_t value) noexcept {
  return static_cast<float>(value) / 8388608.0F;
}

void write_int24_le(std::int32_t value, std::uint8_t* bytes) noexcept {
  bytes[0] = static_cast<std::uint8_t>(value & 0xFF);
  bytes[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
  bytes[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
}

std::int32_t float_to_int24(float value) noexcept {
  const auto clamped = std::clamp(value, -1.0F, 1.0F);
  return static_cast<std::int32_t>(
      std::llround(static_cast<double>(clamped) * 8388607.0));
}

float int32_to_float(std::int32_t value) noexcept {
  return static_cast<float>(value) / 2147483648.0F;
}

std::int32_t float_to_int32(float value) noexcept {
  const auto clamped = std::clamp(value, -1.0F, 1.0F);
  return static_cast<std::int32_t>(
      std::llround(static_cast<double>(clamped) * 2147483647.0));
}

}  // namespace

SampleConversionResult SampleConversionResult::success() {
  return SampleConversionResult(SampleConversionStatus::Ok);
}

SampleConversionResult SampleConversionResult::failure(SampleConversionStatus status) {
  return SampleConversionResult(status);
}

bool SampleConversionResult::ok() const noexcept {
  return status_ == SampleConversionStatus::Ok;
}

SampleConversionStatus SampleConversionResult::status() const noexcept {
  return status_;
}

SampleConversionResult::SampleConversionResult(SampleConversionStatus status)
    : status_(status) {}

std::size_t required_interleaved_bytes(const AudioFormat& format,
                                       std::size_t frames) noexcept {
  return static_cast<std::size_t>(format.channels) * frames * bytes_per_sample(format);
}

SampleConversionResult import_interleaved_to_float(const void* source,
                                                   std::size_t source_bytes,
                                                   const AudioFormat& source_format,
                                                   realtime::AudioBuffer& destination,
                                                   std::size_t frames) noexcept {
  if (!is_supported(source_format)) {
    return SampleConversionResult::failure(SampleConversionStatus::UnsupportedFormat);
  }
  if (source_format.channels != destination.channels()) {
    return SampleConversionResult::failure(SampleConversionStatus::ChannelMismatch);
  }
  if (frames > destination.frames() ||
      source_bytes < required_interleaved_bytes(source_format, frames)) {
    return SampleConversionResult::failure(SampleConversionStatus::BufferTooSmall);
  }

  if (source_format.sample_format == AudioSampleFormat::IeeeFloat) {
    const auto* samples = static_cast<const float*>(source);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
        destination.channel(channel)[frame] =
            samples[(frame * destination.channels()) + channel];
      }
    }
    return SampleConversionResult::success();
  }

  if (source_format.bits_per_sample == 16) {
    const auto* samples = static_cast<const std::int16_t*>(source);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
        destination.channel(channel)[frame] =
            int16_to_float(samples[(frame * destination.channels()) + channel]);
      }
    }
  } else if (source_format.bits_per_sample == 24) {
    const auto* bytes = static_cast<const std::uint8_t*>(source);
    const auto channels = destination.channels();
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto sample_index = (frame * channels) + channel;
        destination.channel(channel)[frame] =
            int24_to_float(read_int24_le(bytes + (sample_index * 3)));
      }
    }
  } else {
    const auto* samples = static_cast<const std::int32_t*>(source);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
        destination.channel(channel)[frame] =
            int32_to_float(samples[(frame * destination.channels()) + channel]);
      }
    }
  }
  return SampleConversionResult::success();
}

SampleConversionResult import_interleaved_to_float(const void* source,
                                                   std::size_t source_bytes,
                                                   const AudioFormat& source_format,
                                                   realtime::AudioBuffer& destination) noexcept {
  return import_interleaved_to_float(source,
                                     source_bytes,
                                     source_format,
                                     destination,
                                     destination.frames());
}

SampleConversionResult export_float_to_interleaved(const realtime::AudioBuffer& source,
                                                   const AudioFormat& destination_format,
                                                   void* destination,
                                                   std::size_t destination_bytes,
                                                   std::size_t frames) noexcept {
  if (!is_supported(destination_format)) {
    return SampleConversionResult::failure(SampleConversionStatus::UnsupportedFormat);
  }
  if (destination_format.channels != source.channels()) {
    return SampleConversionResult::failure(SampleConversionStatus::ChannelMismatch);
  }
  if (frames > source.frames() ||
      destination_bytes < required_interleaved_bytes(destination_format, frames)) {
    return SampleConversionResult::failure(SampleConversionStatus::BufferTooSmall);
  }

  if (destination_format.sample_format == AudioSampleFormat::IeeeFloat) {
    auto* samples = static_cast<float*>(destination);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < source.channels(); ++channel) {
        samples[(frame * source.channels()) + channel] = source.channel(channel)[frame];
      }
    }
    return SampleConversionResult::success();
  }

  if (destination_format.bits_per_sample == 16) {
    auto* samples = static_cast<std::int16_t*>(destination);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < source.channels(); ++channel) {
        samples[(frame * source.channels()) + channel] =
            float_to_int16(source.channel(channel)[frame]);
      }
    }
  } else if (destination_format.bits_per_sample == 24) {
    auto* bytes = static_cast<std::uint8_t*>(destination);
    const auto channels = source.channels();
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < channels; ++channel) {
        const auto sample_index = (frame * channels) + channel;
        write_int24_le(float_to_int24(source.channel(channel)[frame]),
                       bytes + (sample_index * 3));
      }
    }
  } else {
    auto* samples = static_cast<std::int32_t*>(destination);
    for (std::size_t frame = 0; frame < frames; ++frame) {
      for (std::size_t channel = 0; channel < source.channels(); ++channel) {
        samples[(frame * source.channels()) + channel] =
            float_to_int32(source.channel(channel)[frame]);
      }
    }
  }
  return SampleConversionResult::success();
}

SampleConversionResult export_float_to_interleaved(const realtime::AudioBuffer& source,
                                                   const AudioFormat& destination_format,
                                                   void* destination,
                                                   std::size_t destination_bytes) noexcept {
  return export_float_to_interleaved(source,
                                     destination_format,
                                     destination,
                                     destination_bytes,
                                     source.frames());
}

}  // namespace sar::platform
