#include "core/control/preset_file_codec.h"

#include "core/control/control_response.h"
#include "core/control/control_wire_protocol.h"

#include <utility>

namespace sar::control {

namespace {

PresetFileError wire_error(std::string message) {
  return {"invalid_preset_file", std::move(message)};
}

}  // namespace

PresetFileEncodeResult PresetFileEncodeResult::success(
    std::vector<std::uint8_t> bytes) {
  return {std::move(bytes), {}, true};
}

PresetFileEncodeResult PresetFileEncodeResult::failure(PresetFileError error) {
  return {{}, std::move(error), false};
}

bool PresetFileEncodeResult::ok() const noexcept { return succeeded_; }

const std::vector<std::uint8_t>& PresetFileEncodeResult::bytes() const noexcept {
  return bytes_;
}

std::vector<std::uint8_t> PresetFileEncodeResult::take_bytes() noexcept {
  return std::move(bytes_);
}

const PresetFileError& PresetFileEncodeResult::error() const noexcept {
  return error_;
}

PresetFileEncodeResult::PresetFileEncodeResult(
    std::vector<std::uint8_t> bytes,
    PresetFileError error,
    bool succeeded) noexcept
    : bytes_(std::move(bytes)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

PresetFileDecodeResult PresetFileDecodeResult::success(PresetDocument preset) {
  return {std::move(preset), {}, true};
}

PresetFileDecodeResult PresetFileDecodeResult::failure(PresetFileError error) {
  return {{}, std::move(error), false};
}

bool PresetFileDecodeResult::ok() const noexcept { return succeeded_; }

const PresetDocument& PresetFileDecodeResult::preset() const noexcept {
  return preset_;
}

PresetDocument PresetFileDecodeResult::take_preset() noexcept {
  return std::move(preset_);
}

const PresetFileError& PresetFileDecodeResult::error() const noexcept {
  return error_;
}

PresetFileDecodeResult::PresetFileDecodeResult(PresetDocument preset,
                                               PresetFileError error,
                                               bool succeeded) noexcept
    : preset_(std::move(preset)),
      error_(std::move(error)),
      succeeded_(succeeded) {}

PresetFileEncodeResult encode_preset_file(const PresetDocument& preset) {
  const auto validation = validate_preset(preset);
  if (!validation.ok()) {
    return PresetFileEncodeResult::failure({
        "invalid_preset",
        validation.errors().empty()
            ? "Preset validation failed."
            : validation.errors().front().message,
    });
  }

  auto response = preset_response("preset-file", preset);
  const auto encoded = encode_control_response(response);
  if (!encoded.ok()) {
    return PresetFileEncodeResult::failure(
        wire_error("Preset exceeds the supported file format limits."));
  }
  return PresetFileEncodeResult::success(encoded.bytes);
}

PresetFileDecodeResult decode_preset_file(
    std::span<const std::uint8_t> bytes) {
  if (bytes.empty()) {
    return PresetFileDecodeResult::failure(
        wire_error("Preset file is empty."));
  }
  if (bytes.size() > kControlWireMaxMessageBytes) {
    return PresetFileDecodeResult::failure(
        wire_error("Preset file exceeds the maximum supported size."));
  }

  const auto decoded = decode_control_response(bytes);
  if (!decoded.ok()) {
    return PresetFileDecodeResult::failure(
        wire_error("Preset file header or payload is invalid."));
  }
  if (decoded.response.status != ControlResponseStatus::Accepted ||
      !decoded.response.has_preset) {
    return PresetFileDecodeResult::failure(
        wire_error("Preset file does not contain a preset document."));
  }

  const auto validation = validate_preset(decoded.response.preset);
  if (!validation.ok()) {
    return PresetFileDecodeResult::failure({
        "invalid_preset",
        validation.errors().empty()
            ? "Preset validation failed."
            : validation.errors().front().message,
    });
  }
  return PresetFileDecodeResult::success(decoded.response.preset);
}

}  // namespace sar::control
