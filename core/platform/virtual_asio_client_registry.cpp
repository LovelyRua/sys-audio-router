#include "core/platform/virtual_asio_client_registry.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sar::platform {

namespace {

bool shares_clock_domain(const VirtualAsioFormat& active,
                         const VirtualAsioFormat& candidate) noexcept {
  return active.sample_rate == candidate.sample_rate &&
         active.input_channels == candidate.input_channels &&
         active.output_channels == candidate.output_channels;
}

}  // namespace

std::vector<VirtualAsioClientError> validate_virtual_asio_client_request(
    const VirtualAsioClientRequest& request) {
  std::vector<VirtualAsioClientError> errors;
  if (request.client_id.empty()) {
    errors.push_back({"empty_asio_client_id", "ASIO client ID must not be empty."});
  } else if (request.client_id.size() > kVirtualAsioMaxClientIdBytes) {
    errors.push_back({"asio_client_id_too_long",
                      "ASIO client ID exceeds the bounded transport limit."});
  }
  if (request.process_id == 0) {
    errors.push_back({"invalid_asio_process_id",
                      "ASIO client process ID must be non-zero."});
  }
  if (request.format.sample_rate == 0) {
    errors.push_back({"invalid_asio_sample_rate",
                      "ASIO sample rate must be non-zero."});
  }
  if (request.format.frames_per_block == 0 ||
      request.format.frames_per_block > kVirtualAsioMaxFramesPerBlock) {
    errors.push_back({"invalid_asio_block_size",
                      "ASIO block size is outside the supported range."});
  }
  if (request.format.input_channels == 0 &&
      request.format.output_channels == 0) {
    errors.push_back({"empty_asio_channel_layout",
                      "ASIO clients must expose input or output channels."});
  }
  if (request.format.input_channels > kVirtualAsioMaxChannels ||
      request.format.output_channels > kVirtualAsioMaxChannels) {
    errors.push_back({"asio_channel_limit_exceeded",
                      "ASIO channel count exceeds the fixed transport limit."});
  }
  return errors;
}

VirtualAsioClientConnectResult VirtualAsioClientConnectResult::success(
    VirtualAsioClientDescriptor client) {
  return {std::move(client), {}};
}

VirtualAsioClientConnectResult VirtualAsioClientConnectResult::failure(
    std::vector<VirtualAsioClientError> errors) {
  return {std::nullopt, std::move(errors)};
}

bool VirtualAsioClientConnectResult::ok() const noexcept {
  return client_.has_value() && errors_.empty();
}

const VirtualAsioClientDescriptor& VirtualAsioClientConnectResult::client()
    const noexcept {
  return *client_;
}

const std::vector<VirtualAsioClientError>&
VirtualAsioClientConnectResult::errors() const noexcept {
  return errors_;
}

VirtualAsioClientConnectResult::VirtualAsioClientConnectResult(
    std::optional<VirtualAsioClientDescriptor> client,
    std::vector<VirtualAsioClientError> errors)
    : client_(std::move(client)), errors_(std::move(errors)) {}

VirtualAsioClientDisconnectResult VirtualAsioClientDisconnectResult::success() {
  return VirtualAsioClientDisconnectResult({});
}

VirtualAsioClientDisconnectResult VirtualAsioClientDisconnectResult::failure(
    std::vector<VirtualAsioClientError> errors) {
  return VirtualAsioClientDisconnectResult(std::move(errors));
}

bool VirtualAsioClientDisconnectResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<VirtualAsioClientError>&
VirtualAsioClientDisconnectResult::errors() const noexcept {
  return errors_;
}

VirtualAsioClientDisconnectResult::VirtualAsioClientDisconnectResult(
    std::vector<VirtualAsioClientError> errors)
    : errors_(std::move(errors)) {}

VirtualAsioClientRegistry::VirtualAsioClientRegistry(
    std::size_t maximum_clients,
    std::uint64_t initial_connection_generation)
    : maximum_clients_(maximum_clients),
      next_generation_(initial_connection_generation == 0
                           ? 1
                           : initial_connection_generation) {
  if (maximum_clients == 0) {
    throw std::invalid_argument(
        "VirtualAsioClientRegistry maximum client count must be non-zero");
  }
  clients_.reserve(maximum_clients);
}

VirtualAsioClientConnectResult VirtualAsioClientRegistry::connect(
    VirtualAsioClientRequest request) {
  auto errors = validate_virtual_asio_client_request(request);
  if (!errors.empty()) {
    return VirtualAsioClientConnectResult::failure(std::move(errors));
  }

  const auto duplicate = std::ranges::find_if(clients_, [&](const auto& client) {
    return client.client_id == request.client_id;
  });
  if (duplicate != clients_.end()) {
    return VirtualAsioClientConnectResult::failure({
        {"duplicate_asio_client_id", "ASIO client ID is already connected."},
    });
  }
  if (clients_.size() >= maximum_clients_) {
    return VirtualAsioClientConnectResult::failure({
        {"asio_client_capacity_reached",
         "ASIO client registry has reached its configured capacity."},
    });
  }
  if (active_format_.has_value() &&
      !shares_clock_domain(*active_format_, request.format)) {
    return VirtualAsioClientConnectResult::failure({
        {"asio_session_format_mismatch",
         "ASIO client format does not match the active session clock domain."},
    });
  }

  if (!active_format_.has_value()) {
    active_format_ = request.format;
  }
  VirtualAsioClientDescriptor client{
      .client_id = std::move(request.client_id),
      .process_id = request.process_id,
      .format = request.format,
      .connection_generation = next_generation_,
  };
  ++next_generation_;
  if (next_generation_ == 0) {
    next_generation_ = 1;
  }
  clients_.push_back(client);
  return VirtualAsioClientConnectResult::success(std::move(client));
}

VirtualAsioClientDisconnectResult VirtualAsioClientRegistry::disconnect(
    const std::string& client_id,
    std::uint64_t connection_generation) {
  const auto client = std::ranges::find_if(clients_, [&](const auto& candidate) {
    return candidate.client_id == client_id;
  });
  if (client == clients_.end()) {
    return VirtualAsioClientDisconnectResult::failure({
        {"unknown_asio_client", "ASIO client is not connected."},
    });
  }
  if (client->connection_generation != connection_generation) {
    return VirtualAsioClientDisconnectResult::failure({
        {"stale_asio_connection",
         "ASIO disconnect generation does not match the active connection."},
    });
  }

  clients_.erase(client);
  if (clients_.empty()) {
    active_format_.reset();
  }
  return VirtualAsioClientDisconnectResult::success();
}

std::size_t VirtualAsioClientRegistry::maximum_clients() const noexcept {
  return maximum_clients_;
}

const std::vector<VirtualAsioClientDescriptor>&
VirtualAsioClientRegistry::clients() const noexcept {
  return clients_;
}

const std::optional<VirtualAsioFormat>&
VirtualAsioClientRegistry::active_format() const noexcept {
  return active_format_;
}

}  // namespace sar::platform
