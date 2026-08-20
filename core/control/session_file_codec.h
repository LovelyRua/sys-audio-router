#pragma once

#include "core/control/session_document.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sar::control {

inline constexpr std::uint16_t kSessionFileVersion = 4;
inline constexpr std::size_t kSessionFileMaxBytes = 1024 * 1024;

struct SessionFileError {
  std::string code;
  std::string message;
};

class SessionFileEncodeResult {
 public:
  static SessionFileEncodeResult success(std::vector<std::uint8_t> bytes);
  static SessionFileEncodeResult failure(SessionFileError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept;
  [[nodiscard]] std::vector<std::uint8_t> take_bytes() noexcept;
  [[nodiscard]] const SessionFileError& error() const noexcept;

 private:
  SessionFileEncodeResult(std::vector<std::uint8_t> bytes,
                          SessionFileError error,
                          bool succeeded) noexcept;

  std::vector<std::uint8_t> bytes_;
  SessionFileError error_;
  bool succeeded_ = false;
};

class SessionFileDecodeResult {
 public:
  static SessionFileDecodeResult success(SessionDocument session);
  static SessionFileDecodeResult failure(SessionFileError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const SessionDocument& session() const noexcept;
  [[nodiscard]] SessionDocument take_session() noexcept;
  [[nodiscard]] const SessionFileError& error() const noexcept;

 private:
  SessionFileDecodeResult(SessionDocument session,
                          SessionFileError error,
                          bool succeeded) noexcept;

  SessionDocument session_;
  SessionFileError error_;
  bool succeeded_ = false;
};

[[nodiscard]] SessionFileEncodeResult encode_session_file(
    const SessionDocument& session);
[[nodiscard]] SessionFileDecodeResult decode_session_file(
    std::span<const std::uint8_t> bytes);

}  // namespace sar::control
