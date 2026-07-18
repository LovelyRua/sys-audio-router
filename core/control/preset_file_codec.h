#pragma once

#include "core/control/preset_document.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sar::control {

struct PresetFileError {
  std::string code;
  std::string message;
};

class PresetFileEncodeResult {
 public:
  static PresetFileEncodeResult success(std::vector<std::uint8_t> bytes);
  static PresetFileEncodeResult failure(PresetFileError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> take_bytes() noexcept;
  [[nodiscard]] const PresetFileError& error() const noexcept;

 private:
  PresetFileEncodeResult(std::vector<std::uint8_t> bytes,
                         PresetFileError error,
                         bool succeeded) noexcept;

  std::vector<std::uint8_t> bytes_;
  PresetFileError error_;
  bool succeeded_ = false;
};

class PresetFileDecodeResult {
 public:
  static PresetFileDecodeResult success(PresetDocument preset);
  static PresetFileDecodeResult failure(PresetFileError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const PresetDocument& preset() const noexcept;
  [[nodiscard]] PresetDocument take_preset() noexcept;
  [[nodiscard]] const PresetFileError& error() const noexcept;

 private:
  PresetFileDecodeResult(PresetDocument preset,
                         PresetFileError error,
                         bool succeeded) noexcept;

  PresetDocument preset_;
  PresetFileError error_;
  bool succeeded_ = false;
};

[[nodiscard]] PresetFileEncodeResult encode_preset_file(
    const PresetDocument& preset);
[[nodiscard]] PresetFileDecodeResult decode_preset_file(
    std::span<const std::uint8_t> bytes);

}  // namespace sar::control
