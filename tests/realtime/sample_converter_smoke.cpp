#include "core/platform/sample_converter.h"

#include "tests/realtime/test_helpers.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <vector>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::AudioFormat float32_format() {
  sar::platform::AudioFormat format;
  format.sample_rate = 48000;
  format.channels = 2;
  format.frames_per_block = 4;
  format.bits_per_sample = 32;
  format.sample_format = sar::platform::AudioSampleFormat::IeeeFloat;
  return format;
}

sar::platform::AudioFormat int16_format() {
  auto format = float32_format();
  format.bits_per_sample = 16;
  format.sample_format = sar::platform::AudioSampleFormat::PcmInt;
  return format;
}

sar::platform::AudioFormat int32_format() {
  auto format = float32_format();
  format.bits_per_sample = 32;
  format.sample_format = sar::platform::AudioSampleFormat::PcmInt;
  return format;
}

sar::platform::AudioFormat int24_in_int32_format() {
  auto format = int32_format();
  format.valid_bits_per_sample = 24;
  return format;
}

sar::platform::AudioFormat int24_format() {
  auto format = float32_format();
  format.bits_per_sample = 24;
  format.sample_format = sar::platform::AudioSampleFormat::PcmInt;
  return format;
}

}  // namespace

int main() {
  {
    const auto format = float32_format();
    const std::vector<float> interleaved = {
        0.25F, -0.25F,
        0.5F, -0.5F,
        0.75F, -0.75F,
        1.0F, -1.0F,
    };
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        interleaved.data(),
        interleaved.size() * sizeof(float),
        format,
        buffer);
    if (const auto failure = expect(result.ok(), "Expected float32 import success")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(0)[2], 0.75F),
                   "Expected channel 0 sample")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(1)[2], -0.75F),
                   "Expected channel 1 sample")) {
      return failure;
    }
  }

  {
    const auto format = int16_format();
    const std::vector<std::int16_t> interleaved = {
        0, 32767,
        -32768, 16384,
        -16384, 8192,
        4096, -4096,
    };
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        interleaved.data(),
        interleaved.size() * sizeof(std::int16_t),
        format,
        buffer);
    if (const auto failure = expect(result.ok(), "Expected int16 import success")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(0)[1], -1.0F),
                   "Expected int16 minimum to import as -1")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(1)[1], 0.5F),
                   "Expected int16 positive sample")) {
      return failure;
    }
  }

  {
    const auto format = int32_format();
    const std::vector<std::int32_t> interleaved = {
        0, std::numeric_limits<std::int32_t>::max(),
        std::numeric_limits<std::int32_t>::min(), 1073741824,
        -1073741824, 536870912,
        268435456, -268435456,
    };
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        interleaved.data(),
        interleaved.size() * sizeof(std::int32_t),
        format,
        buffer);
    if (const auto failure = expect(result.ok(), "Expected int32 import success")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(0)[1], -1.0F),
                   "Expected int32 minimum to import as -1")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(1)[1], 0.5F),
                   "Expected int32 positive sample")) {
      return failure;
    }
  }

  {
    const auto format = int24_in_int32_format();
    if (const auto failure = expect(sar::platform::required_interleaved_bytes(format, 4) == 32,
                                    "Expected int24-in-int32 byte count")) {
      return failure;
    }

    const std::vector<std::int32_t> interleaved = {
        0, 0x7FFFFF00,
        static_cast<std::int32_t>(0x80000000), 0x40000000,
        static_cast<std::int32_t>(0xC0000000), 0x20000000,
        0x10000000, static_cast<std::int32_t>(0xF0000000),
    };
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        interleaved.data(),
        interleaved.size() * sizeof(std::int32_t),
        format,
        buffer);
    if (const auto failure = expect(result.ok(), "Expected int24-in-int32 import success")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(0)[1], -1.0F),
                   "Expected int24-in-int32 minimum to import as -1")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(1)[1], 0.5F),
                   "Expected int24-in-int32 positive sample")) {
      return failure;
    }
  }

  {
    const auto format = int16_format();
    sar::realtime::AudioBuffer buffer(2, 2);
    buffer.channel(0)[0] = -1.0F;
    buffer.channel(1)[0] = 1.0F;
    buffer.channel(0)[1] = 2.0F;
    buffer.channel(1)[1] = -2.0F;

    std::vector<std::int16_t> interleaved(4);
    const auto result = sar::platform::export_float_to_interleaved(
        buffer,
        format,
        interleaved.data(),
        interleaved.size() * sizeof(std::int16_t));
    if (const auto failure = expect(result.ok(), "Expected int16 export success")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[0] == -32767,
                                    "Expected negative full-scale export")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[1] == 32767,
                                    "Expected positive full-scale export")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[2] == 32767 && interleaved[3] == -32767,
                                    "Expected export clipping")) {
      return failure;
    }
  }

  {
    const auto format = int24_in_int32_format();
    sar::realtime::AudioBuffer buffer(2, 2);
    buffer.channel(0)[0] = -1.0F;
    buffer.channel(1)[0] = 1.0F;
    buffer.channel(0)[1] = 2.0F;
    buffer.channel(1)[1] = -2.0F;

    std::vector<std::int32_t> interleaved(4);
    const auto result = sar::platform::export_float_to_interleaved(
        buffer,
        format,
        interleaved.data(),
        interleaved.size() * sizeof(std::int32_t));
    if (const auto failure = expect(result.ok(), "Expected int24-in-int32 export success")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[0] == static_cast<std::int32_t>(0x80000100),
                                    "Expected negative int24-in-int32 full-scale export")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[1] == 0x7FFFFF00,
                                    "Expected positive int24-in-int32 full-scale export")) {
      return failure;
    }
    if (const auto failure =
            expect(interleaved[2] == 0x7FFFFF00 &&
                       interleaved[3] == static_cast<std::int32_t>(0x80000100),
                   "Expected int24-in-int32 export clipping")) {
      return failure;
    }
    if (const auto failure = expect((interleaved[0] & 0xFF) == 0 &&
                                        (interleaved[1] & 0xFF) == 0 &&
                                        (interleaved[2] & 0xFF) == 0 &&
                                        (interleaved[3] & 0xFF) == 0,
                                    "Expected int24-in-int32 low padding bits to be zero")) {
      return failure;
    }
  }

  {
    const auto format = int32_format();
    sar::realtime::AudioBuffer buffer(2, 2);
    buffer.channel(0)[0] = -1.0F;
    buffer.channel(1)[0] = 1.0F;
    buffer.channel(0)[1] = 2.0F;
    buffer.channel(1)[1] = -2.0F;

    std::vector<std::int32_t> interleaved(4);
    const auto result = sar::platform::export_float_to_interleaved(
        buffer,
        format,
        interleaved.data(),
        interleaved.size() * sizeof(std::int32_t));
    if (const auto failure = expect(result.ok(), "Expected int32 export success")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[0] == -2147483647,
                                    "Expected negative int32 full-scale export")) {
      return failure;
    }
    if (const auto failure =
            expect(interleaved[1] == std::numeric_limits<std::int32_t>::max(),
                                    "Expected positive int32 full-scale export")) {
      return failure;
    }
    if (const auto failure =
            expect(interleaved[2] == std::numeric_limits<std::int32_t>::max() &&
                       interleaved[3] == -2147483647,
                   "Expected int32 export clipping")) {
      return failure;
    }
  }

  {
    const auto format = int24_format();
    if (const auto failure = expect(sar::platform::required_interleaved_bytes(format, 4) == 24,
                                    "Expected packed int24 byte count")) {
      return failure;
    }

    const std::vector<std::uint8_t> interleaved = {
        0x00, 0x00, 0x00, 0xFF, 0xFF, 0x7F,
        0x00, 0x00, 0x80, 0x00, 0x00, 0x40,
        0x00, 0x00, 0xC0, 0x00, 0x00, 0x20,
        0x00, 0x00, 0x10, 0x00, 0x00, 0xF0,
    };
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        interleaved.data(),
        interleaved.size(),
        format,
        buffer);
    if (const auto failure = expect(result.ok(), "Expected int24 import success")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(0)[1], -1.0F),
                   "Expected int24 minimum to import as -1")) {
      return failure;
    }
    if (const auto failure =
            expect(sar::tests::nearly_equal(buffer.channel(1)[1], 0.5F),
                   "Expected int24 positive sample")) {
      return failure;
    }
  }

  {
    const auto format = int24_format();
    sar::realtime::AudioBuffer buffer(2, 2);
    buffer.channel(0)[0] = -1.0F;
    buffer.channel(1)[0] = 1.0F;
    buffer.channel(0)[1] = 2.0F;
    buffer.channel(1)[1] = -2.0F;

    std::vector<std::uint8_t> interleaved(12);
    const auto result = sar::platform::export_float_to_interleaved(
        buffer,
        format,
        interleaved.data(),
        interleaved.size());
    if (const auto failure = expect(result.ok(), "Expected int24 export success")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[0] == 0x01 && interleaved[1] == 0x00 &&
                                        interleaved[2] == 0x80,
                                    "Expected negative int24 full-scale export")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[3] == 0xFF && interleaved[4] == 0xFF &&
                                        interleaved[5] == 0x7F,
                                    "Expected positive int24 full-scale export")) {
      return failure;
    }
    if (const auto failure = expect(interleaved[6] == 0xFF && interleaved[7] == 0xFF &&
                                        interleaved[8] == 0x7F &&
                                        interleaved[9] == 0x01 &&
                                        interleaved[10] == 0x00 &&
                                        interleaved[11] == 0x80,
                                    "Expected int24 export clipping")) {
      return failure;
    }
  }

  {
    const auto format = float32_format();
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::import_interleaved_to_float(
        nullptr,
        sar::platform::required_interleaved_bytes(format, 4),
        format,
        buffer);
    if (const auto failure =
            expect(result.status() == sar::platform::SampleConversionStatus::NullBuffer,
                   "Expected null source import status")) {
      return failure;
    }
  }

  {
    const auto format = float32_format();
    sar::realtime::AudioBuffer buffer(2, 4);
    const auto result = sar::platform::export_float_to_interleaved(
        buffer,
        format,
        nullptr,
        sar::platform::required_interleaved_bytes(format, 4));
    if (const auto failure =
            expect(result.status() == sar::platform::SampleConversionStatus::NullBuffer,
                   "Expected null destination export status")) {
      return failure;
    }
  }

  std::cout << "Sample converter smoke test passed\n";
  return 0;
}
