#pragma once

#include "core/platform/virtual_asio_client_registry.h"
#include "core/platform/windows_virtual_asio_object_names.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace sar::control {

inline constexpr std::uint16_t kVirtualAsioBrokerVersion = 1;
inline constexpr std::size_t kVirtualAsioBrokerHeaderBytes = 20;
inline constexpr std::size_t kVirtualAsioBrokerMaxMessageBytes = 16 * 1024;
inline constexpr std::size_t kVirtualAsioBrokerMaxStringBytes = 512;

enum class VirtualAsioBrokerErrorCode {
  None,
  MessageTooLarge,
  Truncated,
  InvalidMagic,
  UnsupportedVersion,
  UnexpectedMessageType,
  InvalidLength,
  StringTooLong,
  InvalidBoolean,
  TrailingBytes,
};

struct VirtualAsioBrokerWireError {
  VirtualAsioBrokerErrorCode code = VirtualAsioBrokerErrorCode::None;
  std::size_t offset = 0;
};

struct VirtualAsioBrokerConnectRequest {
  std::uint64_t request_id = 0;
  std::string client_id;
  platform::VirtualAsioFormat format;
  std::uint32_t queue_capacity_blocks = 0;
  std::uint64_t client_nonce_low = 0;
  std::uint64_t client_nonce_high = 0;
};

struct VirtualAsioBrokerDisconnectRequest {
  std::uint64_t request_id = 0;
  std::string client_id;
  std::uint64_t connection_generation = 0;
};

struct VirtualAsioBrokerFormatRequest {
  std::uint64_t request_id = 0;
};

struct VirtualAsioBrokerConnectResponse {
  std::uint64_t request_id = 0;
  bool accepted = false;
  std::uint64_t connection_generation = 0;
  platform::WindowsVirtualAsioObjectNames names;
  std::uint64_t server_nonce_low = 0;
  std::uint64_t server_nonce_high = 0;
  std::string error_code;
  std::string error_message;
};

struct VirtualAsioBrokerDisconnectResponse {
  std::uint64_t request_id = 0;
  bool accepted = false;
  std::string error_code;
  std::string error_message;
};

struct VirtualAsioBrokerFormatResponse {
  std::uint64_t request_id = 0;
  bool accepted = false;
  platform::VirtualAsioFormat format;
  std::string error_code;
  std::string error_message;
};

struct VirtualAsioBrokerEncodeResult {
  std::vector<std::uint8_t> bytes;
  VirtualAsioBrokerWireError error;
  [[nodiscard]] bool ok() const noexcept {
    return error.code == VirtualAsioBrokerErrorCode::None;
  }
};

template <typename T>
struct VirtualAsioBrokerDecodeResult {
  T value;
  VirtualAsioBrokerWireError error;
  [[nodiscard]] bool ok() const noexcept {
    return error.code == VirtualAsioBrokerErrorCode::None;
  }
};

[[nodiscard]] VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_connect(
    const VirtualAsioBrokerConnectRequest& request);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerConnectRequest>
decode_virtual_asio_broker_connect(std::span<const std::uint8_t> bytes);

[[nodiscard]] VirtualAsioBrokerEncodeResult
encode_virtual_asio_broker_disconnect(
    const VirtualAsioBrokerDisconnectRequest& request);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerDisconnectRequest>
decode_virtual_asio_broker_disconnect(std::span<const std::uint8_t> bytes);

[[nodiscard]] VirtualAsioBrokerEncodeResult encode_virtual_asio_broker_format(
    const VirtualAsioBrokerFormatRequest& request);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerFormatRequest>
decode_virtual_asio_broker_format(std::span<const std::uint8_t> bytes);

[[nodiscard]] VirtualAsioBrokerEncodeResult
encode_virtual_asio_broker_connect_response(
    const VirtualAsioBrokerConnectResponse& response);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerConnectResponse>
decode_virtual_asio_broker_connect_response(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] VirtualAsioBrokerEncodeResult
encode_virtual_asio_broker_disconnect_response(
    const VirtualAsioBrokerDisconnectResponse& response);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerDisconnectResponse>
decode_virtual_asio_broker_disconnect_response(
    std::span<const std::uint8_t> bytes);

[[nodiscard]] VirtualAsioBrokerEncodeResult
encode_virtual_asio_broker_format_response(
    const VirtualAsioBrokerFormatResponse& response);
[[nodiscard]] VirtualAsioBrokerDecodeResult<VirtualAsioBrokerFormatResponse>
decode_virtual_asio_broker_format_response(
    std::span<const std::uint8_t> bytes);

}  // namespace sar::control
