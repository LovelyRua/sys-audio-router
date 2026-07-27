#include "core/control/virtual_asio_broker_protocol.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
  using namespace sar::control;

  const VirtualAsioBrokerConnectRequest connect{
      .request_id = 0x1122334455667788ULL,
      .client_id = "daw-client",
      .format = {48000, 128, 16, 16},
      .queue_capacity_blocks = 8,
      .client_nonce_low = 0x0102030405060708ULL,
      .client_nonce_high = 0x1112131415161718ULL,
  };
  const auto encoded_connect = encode_virtual_asio_broker_connect(connect);
  assert(encoded_connect.ok());
  assert(encoded_connect.bytes.size() <= kVirtualAsioBrokerMaxMessageBytes);
  const auto decoded_connect =
      decode_virtual_asio_broker_connect(encoded_connect.bytes);
  assert(decoded_connect.ok());
  assert(decoded_connect.value.request_id == connect.request_id);
  assert(decoded_connect.value.client_id == connect.client_id);
  assert(decoded_connect.value.format == connect.format);
  assert(decoded_connect.value.queue_capacity_blocks ==
         connect.queue_capacity_blocks);
  assert(decoded_connect.value.client_nonce_low == connect.client_nonce_low);
  assert(decoded_connect.value.client_nonce_high == connect.client_nonce_high);

  for (std::size_t bytes = 0; bytes < encoded_connect.bytes.size(); ++bytes) {
    assert(!decode_virtual_asio_broker_connect(
                std::span(encoded_connect.bytes.data(), bytes))
                .ok());
  }
  assert(!decode_virtual_asio_broker_disconnect(encoded_connect.bytes).ok());

  auto corrupt = encoded_connect.bytes;
  corrupt[0] ^= 0xFFU;
  assert(decode_virtual_asio_broker_connect(corrupt).error.code ==
         VirtualAsioBrokerErrorCode::InvalidMagic);
  corrupt = encoded_connect.bytes;
  corrupt[4] = 0xFFU;
  assert(decode_virtual_asio_broker_connect(corrupt).error.code ==
         VirtualAsioBrokerErrorCode::UnsupportedVersion);
  corrupt = encoded_connect.bytes;
  corrupt[8] ^= 1U;
  assert(decode_virtual_asio_broker_connect(corrupt).error.code ==
         VirtualAsioBrokerErrorCode::InvalidLength);
  corrupt = encoded_connect.bytes;
  corrupt.push_back(0);
  assert(decode_virtual_asio_broker_connect(corrupt).error.code ==
         VirtualAsioBrokerErrorCode::InvalidLength);

  auto oversized_connect = connect;
  oversized_connect.client_id.assign(kVirtualAsioBrokerMaxStringBytes + 1, 'x');
  assert(encode_virtual_asio_broker_connect(oversized_connect).error.code ==
         VirtualAsioBrokerErrorCode::StringTooLong);
  std::vector<std::uint8_t> oversized_message(
      kVirtualAsioBrokerMaxMessageBytes + 1);
  assert(decode_virtual_asio_broker_connect(oversized_message).error.code ==
         VirtualAsioBrokerErrorCode::MessageTooLarge);

  const VirtualAsioBrokerDisconnectRequest disconnect{
      .request_id = 91,
      .client_id = "daw-client",
      .connection_generation = 42,
  };
  const auto encoded_disconnect =
      encode_virtual_asio_broker_disconnect(disconnect);
  assert(encoded_disconnect.ok());
  const auto decoded_disconnect =
      decode_virtual_asio_broker_disconnect(encoded_disconnect.bytes);
  assert(decoded_disconnect.ok());
  assert(decoded_disconnect.value.request_id == disconnect.request_id);
  assert(decoded_disconnect.value.client_id == disconnect.client_id);
  assert(decoded_disconnect.value.connection_generation ==
         disconnect.connection_generation);

  const VirtualAsioBrokerFormatRequest format_request{.request_id = 92};
  const auto format_request_roundtrip = decode_virtual_asio_broker_format(
      encode_virtual_asio_broker_format(format_request).bytes);
  assert(format_request_roundtrip.ok());
  assert(format_request_roundtrip.value.request_id == format_request.request_id);

  const VirtualAsioBrokerConnectResponse accepted{
      .request_id = connect.request_id,
      .accepted = true,
      .connection_generation = 42,
      .names = {L"Local\\mapping", L"Local\\input", L"Local\\output",
                L"Local\\shutdown"},
      .server_nonce_low = 71,
      .server_nonce_high = 72,
  };
  const auto encoded_response =
      encode_virtual_asio_broker_connect_response(accepted);
  assert(encoded_response.ok());
  const auto decoded_response =
      decode_virtual_asio_broker_connect_response(encoded_response.bytes);
  assert(decoded_response.ok());
  assert(decoded_response.value.request_id == accepted.request_id);
  assert(decoded_response.value.accepted);
  assert(decoded_response.value.connection_generation ==
         accepted.connection_generation);
  assert(decoded_response.value.names == accepted.names);
  assert(decoded_response.value.server_nonce_low == accepted.server_nonce_low);
  assert(decoded_response.value.server_nonce_high == accepted.server_nonce_high);

  corrupt = encoded_response.bytes;
  corrupt[kVirtualAsioBrokerHeaderBytes] = 2;
  assert(decode_virtual_asio_broker_connect_response(corrupt).error.code ==
         VirtualAsioBrokerErrorCode::InvalidBoolean);

  const VirtualAsioBrokerConnectResponse rejected{
      .request_id = 100,
      .accepted = false,
      .error_code = "format_mismatch",
      .error_message = "The requested clock domain does not match.",
  };
  const auto rejected_roundtrip = decode_virtual_asio_broker_connect_response(
      encode_virtual_asio_broker_connect_response(rejected).bytes);
  assert(rejected_roundtrip.ok());
  assert(!rejected_roundtrip.value.accepted);
  assert(rejected_roundtrip.value.error_code == rejected.error_code);
  assert(rejected_roundtrip.value.error_message == rejected.error_message);

  const VirtualAsioBrokerDisconnectResponse disconnect_response{
      .request_id = disconnect.request_id,
      .accepted = false,
      .error_code = "stale_asio_connection",
      .error_message = "Connection generation is stale.",
  };
  const auto disconnect_response_roundtrip =
      decode_virtual_asio_broker_disconnect_response(
          encode_virtual_asio_broker_disconnect_response(disconnect_response)
              .bytes);
  assert(disconnect_response_roundtrip.ok());
  assert(disconnect_response_roundtrip.value.request_id ==
         disconnect_response.request_id);
  assert(!disconnect_response_roundtrip.value.accepted);
  assert(disconnect_response_roundtrip.value.error_code ==
         disconnect_response.error_code);

  const VirtualAsioBrokerFormatResponse format_response{
      .request_id = format_request.request_id,
      .accepted = true,
      .format = {96000, 256, 2, 2},
  };
  const auto format_response_roundtrip =
      decode_virtual_asio_broker_format_response(
          encode_virtual_asio_broker_format_response(format_response).bytes);
  assert(format_response_roundtrip.ok());
  assert(format_response_roundtrip.value.request_id ==
         format_response.request_id);
  assert(format_response_roundtrip.value.accepted);
  assert(format_response_roundtrip.value.format == format_response.format);
}
