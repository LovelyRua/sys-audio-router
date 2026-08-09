#pragma once

#include "core/control/control_command.h"
#include "core/control/control_response.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sar::control {

inline constexpr std::uint16_t kControlWireVersion = 8;
inline constexpr std::size_t kControlWireHeaderSize = 12;
inline constexpr std::size_t kControlWireMaxMessageBytes = 1024 * 1024;
inline constexpr std::size_t kControlWireMaxStringBytes = 64 * 1024;
inline constexpr std::size_t kControlWireMaxArrayElements = 4096;

enum class ControlWireErrorCode {
  None,
  MessageTooLarge,
  Truncated,
  InvalidMagic,
  UnsupportedVersion,
  UnexpectedMessageKind,
  InvalidHeader,
  InvalidLength,
  StringTooLong,
  ArrayTooLong,
  UnknownEnum,
  InvalidBoolean,
  InvalidValue,
  TrailingBytes,
};

struct ControlWireError {
  ControlWireErrorCode code = ControlWireErrorCode::None;
  std::size_t offset = 0;
};

struct ControlWireEncodeResult {
  std::vector<std::uint8_t> bytes;
  ControlWireError error;

  [[nodiscard]] bool ok() const noexcept {
    return error.code == ControlWireErrorCode::None;
  }
};

struct ControlCommandDecodeResult {
  ControlCommand command;
  ControlWireError error;

  [[nodiscard]] bool ok() const noexcept {
    return error.code == ControlWireErrorCode::None;
  }
};

struct ControlResponseDecodeResult {
  ControlResponse response;
  ControlWireError error;

  [[nodiscard]] bool ok() const noexcept {
    return error.code == ControlWireErrorCode::None;
  }
};

[[nodiscard]] ControlWireEncodeResult encode_control_command(
    const ControlCommand& command);
[[nodiscard]] ControlCommandDecodeResult decode_control_command(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] ControlWireEncodeResult encode_control_response(
    const ControlResponse& response);
[[nodiscard]] ControlResponseDecodeResult decode_control_response(
    std::span<const std::uint8_t> bytes);

}  // namespace sar::control
