#include "core/control/virtual_asio_broker_protocol.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <vector>

// Deterministic mutation fuzzing of the Virtual ASIO broker wire protocol.
// The broker pipe is a DLL-facing surface: any process that has loaded (or
// impersonates) the driver DLL can send these bytes to the engine, and the
// DLL itself must survive malformed bytes sent back by the broker. Every
// decoder here must reject or accept any input without crashing or hanging,
// the same property control_codec_mutation_smoke.cpp checks for the
// named-pipe control protocol.

namespace {

class Xorshift {
 public:
  explicit Xorshift(std::uint64_t seed) noexcept : state_(seed) {}
  std::uint64_t next() noexcept {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 7U;
    state_ ^= state_ << 17U;
    return state_;
  }
  std::size_t below(std::size_t bound) noexcept {
    return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
  }

 private:
  std::uint64_t state_;
};

std::vector<std::uint8_t> mutate(const std::vector<std::uint8_t>& original,
                                 Xorshift& random) {
  auto bytes = original;
  switch (random.below(6)) {
    case 0:
      if (!bytes.empty()) {
        bytes[random.below(bytes.size())] ^=
            static_cast<std::uint8_t>(1U << random.below(8));
      }
      break;
    case 1:
      if (!bytes.empty()) {
        bytes[random.below(bytes.size())] =
            static_cast<std::uint8_t>(random.next());
      }
      break;
    case 2: {
      static constexpr std::uint32_t kInteresting[] = {
          0U, 1U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU, 0x0000FFFFU};
      if (bytes.size() >= 4) {
        const auto offset = random.below(bytes.size() - 3);
        const auto value = kInteresting[random.below(6)];
        for (std::size_t index = 0; index < 4; ++index) {
          bytes[offset + index] =
              static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
        }
      }
      break;
    }
    case 3:
      bytes.resize(random.below(bytes.size() + 1));
      break;
    case 4: {
      const auto extra = 1 + random.below(16);
      for (std::size_t index = 0; index < extra; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(random.next()));
      }
      break;
    }
    default: {
      const auto edits = 1 + random.below(8);
      for (std::size_t index = 0; index < edits && !bytes.empty(); ++index) {
        bytes[random.below(bytes.size())] =
            static_cast<std::uint8_t>(random.next());
      }
      break;
    }
  }
  return bytes;
}

void fuzz(const char* name,
         const std::vector<std::uint8_t>& valid,
         std::uint64_t seed,
         std::size_t iterations,
         const std::function<bool(const std::vector<std::uint8_t>&)>&
             decode_ok) {
  std::fprintf(stderr, "fuzzing %s\n", name);
  assert(decode_ok(valid));
  Xorshift random(seed);
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const auto input = mutate(valid, random);
    try {
      decode_ok(input);
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s: iteration %zu threw: %s\n", name, iteration,
                   error.what());
      std::fprintf(stderr, "input (%zu bytes):", input.size());
      for (std::size_t index = 0; index < input.size() && index < 96; ++index) {
        std::fprintf(stderr, " %02x", input[index]);
      }
      std::fprintf(stderr, "\n");
      std::abort();
    }
  }
}

}  // namespace

int main() {
  constexpr std::size_t kIterations = 30000;

  sar::control::VirtualAsioBrokerConnectRequest connect;
  connect.request_id = 42;
  connect.client_id = "reaper-x64";
  connect.format = {48000, 128, 8, 8};
  connect.queue_capacity_blocks = 64;
  connect.client_nonce_low = 0x0123456789ABCDEFULL;
  connect.client_nonce_high = 0xFEDCBA9876543210ULL;
  const auto connect_bytes =
      sar::control::encode_virtual_asio_broker_connect(connect);
  assert(connect_bytes.ok());
  fuzz("connect_request", connect_bytes.bytes, 0x5AF1U, kIterations,
      [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_connect(bytes).ok();
      });

  sar::control::VirtualAsioBrokerDisconnectRequest disconnect;
  disconnect.request_id = 7;
  disconnect.client_id = "reaper-x64";
  disconnect.connection_generation = 3;
  const auto disconnect_bytes =
      sar::control::encode_virtual_asio_broker_disconnect(disconnect);
  assert(disconnect_bytes.ok());
  fuzz("disconnect_request", disconnect_bytes.bytes, 0xC0DEU, kIterations,
      [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_disconnect(bytes).ok();
      });

  sar::control::VirtualAsioBrokerFormatRequest format_request;
  format_request.request_id = 99;
  const auto format_request_bytes =
      sar::control::encode_virtual_asio_broker_format(format_request);
  assert(format_request_bytes.ok());
  fuzz("format_request", format_request_bytes.bytes, 0xBEEFU, kIterations,
      [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_format(bytes).ok();
      });

  sar::control::VirtualAsioBrokerConnectResponse connect_response;
  connect_response.request_id = 42;
  connect_response.accepted = true;
  connect_response.connection_generation = 3;
  connect_response.names.mapping = L"Local\\SAR.Map.abc123";
  connect_response.names.input_event = L"Local\\SAR.In.abc123";
  connect_response.names.output_event = L"Local\\SAR.Out.abc123";
  connect_response.names.shutdown_event = L"Local\\SAR.Shutdown.abc123";
  connect_response.names.client_disconnect_event = L"Local\\SAR.Bye.abc123";
  connect_response.server_nonce_low = 0x1122334455667788ULL;
  connect_response.server_nonce_high = 0x99AABBCCDDEEFF00ULL;
  const auto connect_response_bytes =
      sar::control::encode_virtual_asio_broker_connect_response(
          connect_response);
  assert(connect_response_bytes.ok());
  fuzz("connect_response", connect_response_bytes.bytes, 0xFACEU, kIterations,
      [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_connect_response(bytes)
            .ok();
      });

  sar::control::VirtualAsioBrokerDisconnectResponse disconnect_response;
  disconnect_response.request_id = 7;
  disconnect_response.accepted = false;
  disconnect_response.error_code = "unknown_client";
  disconnect_response.error_message = "The client is not connected.";
  const auto disconnect_response_bytes =
      sar::control::encode_virtual_asio_broker_disconnect_response(
          disconnect_response);
  assert(disconnect_response_bytes.ok());
  fuzz("disconnect_response", disconnect_response_bytes.bytes, 0x1234U,
      kIterations, [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_disconnect_response(
                   bytes)
            .ok();
      });

  sar::control::VirtualAsioBrokerFormatResponse format_response;
  format_response.request_id = 99;
  format_response.accepted = true;
  format_response.format = {48000, 128, 8, 8};
  const auto format_response_bytes =
      sar::control::encode_virtual_asio_broker_format_response(
          format_response);
  assert(format_response_bytes.ok());
  fuzz("format_response", format_response_bytes.bytes, 0x9E9EU, kIterations,
      [](const std::vector<std::uint8_t>& bytes) {
        return sar::control::decode_virtual_asio_broker_format_response(bytes)
            .ok();
      });

  return 0;
}
