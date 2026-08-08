#include "core/service/windows_virtual_asio_broker_server.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace sar::service {
namespace {

constexpr std::uint16_t kConnectRequestType = 1;
constexpr std::uint16_t kDisconnectRequestType = 2;
constexpr std::uint16_t kFormatRequestType = 5;
constexpr std::size_t kMaximumGraphBuildAttempts = 3;

std::uint16_t message_type(std::span<const std::byte> bytes) noexcept {
  if (bytes.size() < 8) {
    return 0;
  }
  return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[6])) |
         static_cast<std::uint16_t>(
             std::to_integer<std::uint8_t>(bytes[7]) << 8U);
}

std::vector<std::byte> as_bytes(std::vector<std::uint8_t> bytes) {
  std::vector<std::byte> result(bytes.size());
  for (std::size_t index = 0; index < bytes.size(); ++index) {
    result[index] = static_cast<std::byte>(bytes[index]);
  }
  return result;
}

std::span<const std::uint8_t> as_u8(std::span<const std::byte> bytes) noexcept {
  return {reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()};
}

NamedPipeControlResult encoded(
    control::VirtualAsioBrokerEncodeResult result) {
  if (!result.ok()) {
    return NamedPipeControlResult::failure({
        "virtual_asio_broker_response_encode_failed",
        "Could not encode the Virtual ASIO broker response.",
    });
  }
  return NamedPipeControlResult::success(as_bytes(std::move(result.bytes)));
}

control::VirtualAsioBrokerConnectResponse connect_rejection(
    std::uint64_t request_id,
    std::string code,
    std::string message) {
  return {
      .request_id = request_id,
      .accepted = false,
      .error_code = std::move(code),
      .error_message = std::move(message),
  };
}

}  // namespace

WindowsVirtualAsioBrokerServer::WindowsVirtualAsioBrokerServer(
    std::wstring pipe_name,
    WindowsVirtualAsioTransportHost& host,
    WindowsVirtualAsioGraphFactory graph_factory,
    WindowsVirtualAsioFormatProvider format_provider)
    : host_(host),
      graph_factory_(std::move(graph_factory)),
      format_provider_(std::move(format_provider)),
      pipe_config_{std::move(pipe_name),
                   static_cast<std::uint32_t>(
                       control::kVirtualAsioBrokerMaxMessageBytes)},
      server_(pipe_config_,
              [this](const NamedPipeControlPeer& peer,
                     std::span<const std::byte> request) {
                return handle(peer, request);
              }) {
  if (!graph_factory_) {
    throw std::invalid_argument(
        "Virtual ASIO broker server requires a graph factory");
  }
}

NamedPipeControlResult WindowsVirtualAsioBrokerServer::start() {
  return server_.start();
}

void WindowsVirtualAsioBrokerServer::stop() noexcept {
  server_.stop();
}

bool WindowsVirtualAsioBrokerServer::running() const noexcept {
  return server_.running();
}

NamedPipeControlStats WindowsVirtualAsioBrokerServer::stats() const noexcept {
  return server_.stats();
}

std::vector<NamedPipeControlError>
WindowsVirtualAsioBrokerServer::last_errors() const {
  return server_.last_errors();
}

const NamedPipeControlConfig& WindowsVirtualAsioBrokerServer::pipe_config()
    const noexcept {
  return pipe_config_;
}

NamedPipeControlResult WindowsVirtualAsioBrokerServer::handle(
    const NamedPipeControlPeer& peer,
    std::span<const std::byte> request) {
  switch (message_type(request)) {
    case kConnectRequestType:
      return handle_connect(peer, as_u8(request));
    case kDisconnectRequestType:
      return handle_disconnect(peer, as_u8(request));
    case kFormatRequestType:
      return handle_format(as_u8(request));
    default:
      return encoded(control::encode_virtual_asio_broker_connect_response(
          connect_rejection(0, "invalid_virtual_asio_broker_message",
                            "Broker message type is missing or unsupported.")));
  }
}

NamedPipeControlResult WindowsVirtualAsioBrokerServer::handle_format(
    std::span<const std::uint8_t> request) {
  const auto decoded = control::decode_virtual_asio_broker_format(request);
  if (!decoded.ok()) {
    return encoded(control::encode_virtual_asio_broker_format_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "invalid_virtual_asio_format_request",
        .error_message = "Virtual ASIO format request is malformed.",
    }));
  }
  if (!format_provider_) {
    return encoded(control::encode_virtual_asio_broker_format_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "virtual_asio_format_unavailable",
        .error_message = "The engine did not publish a preferred ASIO format.",
    }));
  }

  platform::VirtualAsioFormat format;
  try {
    format = format_provider_();
  } catch (...) {
    return encoded(control::encode_virtual_asio_broker_format_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "virtual_asio_format_provider_failed",
        .error_message = "The engine could not publish its preferred ASIO format.",
    }));
  }
  const auto errors = platform::validate_virtual_asio_client_request({
      .client_id = "format-probe",
      .process_id = 1,
      .format = format,
  });
  if (!errors.empty()) {
    return encoded(control::encode_virtual_asio_broker_format_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "virtual_asio_format_invalid",
        .error_message = "The engine published an invalid preferred ASIO format.",
    }));
  }
  return encoded(control::encode_virtual_asio_broker_format_response({
      .request_id = decoded.value.request_id,
      .accepted = true,
      .format = format,
  }));
}

NamedPipeControlResult WindowsVirtualAsioBrokerServer::handle_connect(
    const NamedPipeControlPeer& peer,
    std::span<const std::uint8_t> request) {
  const auto decoded = control::decode_virtual_asio_broker_connect(request);
  if (!decoded.ok()) {
    return encoded(control::encode_virtual_asio_broker_connect_response(
        connect_rejection(decoded.value.request_id,
                          "invalid_virtual_asio_connect_request",
                          "Virtual ASIO connect request is malformed.")));
  }
  if (decoded.value.queue_capacity_blocks !=
      host_.config().queue_capacity_blocks) {
    return encoded(control::encode_virtual_asio_broker_connect_response(
        connect_rejection(decoded.value.request_id,
                          "virtual_asio_queue_capacity_mismatch",
                          "Requested queue capacity does not match the host.")));
  }

  WindowsVirtualAsioHostConnectRequest host_request{
      .client = {.client_id = decoded.value.client_id,
                 .process_id = peer.process_id,
                 .format = decoded.value.format},
      .client_nonce_low = decoded.value.client_nonce_low,
      .client_nonce_high = decoded.value.client_nonce_high,
  };
  for (std::size_t attempt = 0; attempt < kMaximumGraphBuildAttempts;
       ++attempt) {
    const auto graph_generation = host_.graph_generation();
    std::unique_ptr<graph::Graph> graph;
    try {
      graph = graph_factory_(decoded.value.format);
    } catch (...) {
      return encoded(control::encode_virtual_asio_broker_connect_response(
          connect_rejection(
              decoded.value.request_id, "virtual_asio_graph_build_failed",
              "Could not build a graph for the requested format.")));
    }
    if (graph == nullptr) {
      return encoded(control::encode_virtual_asio_broker_connect_response(
          connect_rejection(
              decoded.value.request_id, "virtual_asio_graph_build_failed",
              "Could not build a graph for the requested format.")));
    }

    auto connected =
        host_.connect(host_request, std::move(graph), graph_generation);
    if (!connected.ok()) {
      const auto& error = connected.errors().front();
      if (error.code == "virtual_asio_graph_generation_stale" &&
          attempt + 1 < kMaximumGraphBuildAttempts) {
        continue;
      }
      return encoded(control::encode_virtual_asio_broker_connect_response(
          connect_rejection(decoded.value.request_id, error.code,
                            error.message)));
    }

    const auto& connection = connected.connection();
    return encoded(control::encode_virtual_asio_broker_connect_response({
        .request_id = decoded.value.request_id,
        .accepted = true,
        .connection_generation = connection.client.connection_generation,
        .names = connection.names,
        .server_nonce_low = connection.server_nonce_low,
        .server_nonce_high = connection.server_nonce_high,
    }));
  }
  return encoded(control::encode_virtual_asio_broker_connect_response(
      connect_rejection(decoded.value.request_id,
                        "virtual_asio_graph_generation_stale",
                        "The routing graph kept changing; retry the Virtual "
                        "ASIO connection.")));
}

NamedPipeControlResult WindowsVirtualAsioBrokerServer::handle_disconnect(
    const NamedPipeControlPeer& peer,
    std::span<const std::uint8_t> request) {
  const auto decoded = control::decode_virtual_asio_broker_disconnect(request);
  if (!decoded.ok()) {
    return encoded(control::encode_virtual_asio_broker_disconnect_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "invalid_virtual_asio_disconnect_request",
        .error_message = "Virtual ASIO disconnect request is malformed.",
    }));
  }

  const auto connections = host_.connections();
  const auto connection = std::ranges::find_if(
      connections, [&](const WindowsVirtualAsioHostConnection& candidate) {
        return candidate.client.client_id == decoded.value.client_id &&
               candidate.client.connection_generation ==
                   decoded.value.connection_generation;
      });
  if (connection != connections.end() &&
      connection->client.process_id != peer.process_id) {
    return encoded(control::encode_virtual_asio_broker_disconnect_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = "virtual_asio_disconnect_peer_mismatch",
        .error_message =
            "Disconnect requests must originate from the connected process.",
    }));
  }

  const auto disconnected = host_.disconnect(
      decoded.value.client_id, decoded.value.connection_generation);
  if (!disconnected.ok()) {
    const auto& error = disconnected.errors().front();
    return encoded(control::encode_virtual_asio_broker_disconnect_response({
        .request_id = decoded.value.request_id,
        .accepted = false,
        .error_code = error.code,
        .error_message = error.message,
    }));
  }
  return encoded(control::encode_virtual_asio_broker_disconnect_response({
      .request_id = decoded.value.request_id,
      .accepted = true,
  }));
}

}  // namespace sar::service
