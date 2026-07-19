#include "core/control/virtual_asio_broker_protocol.h"
#include "core/graph/graph.h"
#include "core/platform/windows_virtual_asio_shared_memory.h"
#include "core/service/windows_virtual_asio_broker_server.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace {

std::unique_ptr<sar::graph::Graph> make_graph(
    const sar::platform::VirtualAsioFormat& format) {
  auto graph = std::make_unique<sar::graph::Graph>(
      1, format.input_channels, format.frames_per_block, format.sample_rate);
  graph->add_node(std::make_unique<sar::graph::PassthroughNode>());
  return graph;
}

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& bytes) {
  std::vector<std::byte> result(bytes.size());
  std::transform(bytes.begin(), bytes.end(), result.begin(), [](auto value) {
    return static_cast<std::byte>(value);
  });
  return result;
}

std::span<const std::uint8_t> as_u8(
    const std::vector<std::byte>& bytes) noexcept {
  return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

sar::control::VirtualAsioBrokerConnectResponse connect(
    sar::service::WindowsVirtualAsioBrokerServer& server,
    const sar::control::VirtualAsioBrokerConnectRequest& request) {
  const auto encoded = sar::control::encode_virtual_asio_broker_connect(request);
  assert(encoded.ok());
  const auto transaction = sar::service::transact_named_pipe_control(
      server.pipe_config(), as_bytes(encoded.bytes));
  assert(transaction.ok());
  const auto decoded = sar::control::decode_virtual_asio_broker_connect_response(
      as_u8(transaction.payload()));
  assert(decoded.ok());
  return decoded.value;
}

sar::control::VirtualAsioBrokerDisconnectResponse disconnect(
    sar::service::WindowsVirtualAsioBrokerServer& server,
    const sar::control::VirtualAsioBrokerDisconnectRequest& request) {
  const auto encoded =
      sar::control::encode_virtual_asio_broker_disconnect(request);
  assert(encoded.ok());
  const auto transaction = sar::service::transact_named_pipe_control(
      server.pipe_config(), as_bytes(encoded.bytes));
  assert(transaction.ok());
  const auto decoded =
      sar::control::decode_virtual_asio_broker_disconnect_response(
          as_u8(transaction.payload()));
  assert(decoded.ok());
  return decoded.value;
}

}  // namespace

int main() {
  sar::service::WindowsVirtualAsioTransportHost host({
      .endpoint_token = "broker-smoke",
      .maximum_clients = 2,
      .queue_capacity_blocks = 4,
      .wait_timeout_ms = 10,
  });
  sar::service::WindowsVirtualAsioBrokerServer server(
      L"sys-audio-route-asio-broker-smoke-" +
          std::to_wstring(GetCurrentProcessId()),
      host, make_graph);
  assert(server.start().ok());
  assert(server.running());

  const sar::control::VirtualAsioBrokerConnectRequest request{
      .request_id = 101,
      .client_id = "broker-daw",
      .format = {48000, 32, 2, 2},
      .queue_capacity_blocks = 4,
      .client_nonce_low = 0x1020304050607080ULL,
      .client_nonce_high = 0x1122334455667788ULL,
  };
  const auto accepted = connect(server, request);
  assert(accepted.accepted);
  assert(accepted.request_id == request.request_id);
  assert(accepted.connection_generation != 0);
  assert(host.active_session_count() == 1);

  auto mapping = sar::platform::WindowsVirtualAsioSharedMemory::open(
      accepted.names.mapping);
  assert(mapping.ok());
  assert(mapping.mapping().state() ==
         sar::platform::VirtualAsioSharedMemoryState::Ready);
  const auto& header = mapping.mapping().header();
  assert(header.client_process_id == GetCurrentProcessId());
  assert(header.client_nonce_low == request.client_nonce_low);
  assert(header.client_nonce_high == request.client_nonce_high);

  auto mismatched = request;
  mismatched.request_id = 102;
  mismatched.client_id = "wrong-capacity";
  mismatched.queue_capacity_blocks = 8;
  const auto rejected = connect(server, mismatched);
  assert(!rejected.accepted);
  assert(rejected.error_code == "virtual_asio_queue_capacity_mismatch");
  assert(host.active_session_count() == 1);

  const auto stale = disconnect(server, {
      .request_id = 201,
      .client_id = request.client_id,
      .connection_generation = accepted.connection_generation + 1,
  });
  assert(!stale.accepted);
  assert(host.active_session_count() == 1);

  const auto disconnected = disconnect(server, {
      .request_id = 202,
      .client_id = request.client_id,
      .connection_generation = accepted.connection_generation,
  });
  assert(disconnected.accepted);
  assert(host.active_session_count() == 0);

  const auto stats = server.stats();
  assert(stats.completed_requests == 4);
  assert(stats.handler_errors == 0);
  assert(stats.protocol_errors == 0);
  server.stop();
  assert(!server.running());
}
