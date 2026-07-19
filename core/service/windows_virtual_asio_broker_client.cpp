#include "core/service/windows_virtual_asio_broker_client.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace sar::service {
namespace {

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

WindowsVirtualAsioBrokerClientConnectResult failure(
    std::string code, std::string message, std::uint32_t native_error = 0) {
  return WindowsVirtualAsioBrokerClientConnectResult::failure({{
      std::move(code), std::move(message), native_error,
  }});
}

void best_effort_disconnect(
    const NamedPipeControlConfig& pipe_config,
    const std::string& client_id,
    std::uint64_t request_id,
    std::uint64_t generation,
    std::uint32_t timeout_ms) noexcept {
  try {
    const auto encoded = control::encode_virtual_asio_broker_disconnect({
        .request_id = request_id ^ 0x8000000000000000ULL,
        .client_id = client_id,
        .connection_generation = generation,
    });
    if (encoded.ok()) {
      static_cast<void>(transact_named_pipe_control(
          pipe_config, as_bytes(encoded.bytes), timeout_ms));
    }
  } catch (...) {
  }
}

bool matches_header(
    const platform::VirtualAsioSharedMemoryHeader& header,
    const control::VirtualAsioBrokerConnectRequest& request,
    const control::VirtualAsioBrokerConnectResponse& response) noexcept {
  return header.connection_generation == response.connection_generation &&
         header.client_process_id == GetCurrentProcessId() &&
         header.sample_rate == request.format.sample_rate &&
         header.frames_per_block == request.format.frames_per_block &&
         header.input_channels == request.format.input_channels &&
         header.output_channels == request.format.output_channels &&
         header.client_nonce_low == request.client_nonce_low &&
         header.client_nonce_high == request.client_nonce_high &&
         header.server_nonce_low == response.server_nonce_low &&
         header.server_nonce_high == response.server_nonce_high;
}

}  // namespace

WindowsVirtualAsioBrokerClientConnectResult
WindowsVirtualAsioBrokerClientConnectResult::success(
    std::unique_ptr<WindowsVirtualAsioBrokerClient> client) {
  return {std::move(client), {}};
}

WindowsVirtualAsioBrokerClientConnectResult
WindowsVirtualAsioBrokerClientConnectResult::failure(
    std::vector<WindowsVirtualAsioBrokerClientError> errors) {
  return {nullptr, std::move(errors)};
}

WindowsVirtualAsioBrokerClientConnectResult::
    WindowsVirtualAsioBrokerClientConnectResult(
        std::unique_ptr<WindowsVirtualAsioBrokerClient> client,
        std::vector<WindowsVirtualAsioBrokerClientError> errors)
    : client_(std::move(client)), errors_(std::move(errors)) {}

bool WindowsVirtualAsioBrokerClientConnectResult::ok() const noexcept {
  return client_ != nullptr && errors_.empty();
}

WindowsVirtualAsioBrokerClient&
WindowsVirtualAsioBrokerClientConnectResult::client() noexcept {
  return *client_;
}

std::unique_ptr<WindowsVirtualAsioBrokerClient>
WindowsVirtualAsioBrokerClientConnectResult::take_client() noexcept {
  return std::move(client_);
}

const std::vector<WindowsVirtualAsioBrokerClientError>&
WindowsVirtualAsioBrokerClientConnectResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioBrokerClient::WindowsVirtualAsioBrokerClient(
    NamedPipeControlConfig pipe_config,
    std::string client_id,
    std::uint64_t request_id,
    std::uint64_t connection_generation,
    platform::WindowsVirtualAsioObjectNames names,
    std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping,
    std::unique_ptr<platform::WindowsVirtualAsioEvents> events,
    platform::WindowsVirtualAsioSharedQueue input,
    platform::WindowsVirtualAsioSharedQueue output) noexcept
    : pipe_config_(std::move(pipe_config)),
      client_id_(std::move(client_id)),
      request_id_(request_id),
      connection_generation_(connection_generation),
      names_(std::move(names)),
      mapping_(std::move(mapping)),
      events_(std::move(events)),
      input_(std::move(input)),
      output_(std::move(output)),
      connected_(true) {}

WindowsVirtualAsioBrokerClient::~WindowsVirtualAsioBrokerClient() {
  static_cast<void>(disconnect(250));
  close_local();
}

WindowsVirtualAsioBrokerClientConnectResult
WindowsVirtualAsioBrokerClient::connect(
    NamedPipeControlConfig pipe_config,
    control::VirtualAsioBrokerConnectRequest request,
    std::uint32_t timeout_ms) {
  const auto encoded = control::encode_virtual_asio_broker_connect(request);
  if (!encoded.ok()) {
    return failure("virtual_asio_connect_encode_failed",
                   "Could not encode the Virtual ASIO connect request.");
  }
  const auto transaction = transact_named_pipe_control(
      pipe_config, as_bytes(encoded.bytes), timeout_ms);
  if (!transaction.ok()) {
    return failure(transaction.error().code, transaction.error().message,
                   transaction.error().native_win32_code);
  }
  const auto decoded = control::decode_virtual_asio_broker_connect_response(
      as_u8(transaction.payload()));
  if (!decoded.ok()) {
    return failure("virtual_asio_connect_response_invalid",
                   "Virtual ASIO broker returned an invalid response.");
  }
  if (decoded.value.request_id != request.request_id) {
    return failure("virtual_asio_connect_response_mismatch",
                   "Virtual ASIO broker response identity did not match.");
  }
  if (!decoded.value.accepted) {
    return failure(decoded.value.error_code, decoded.value.error_message);
  }

  const auto rollback = [&] {
    best_effort_disconnect(pipe_config, request.client_id, request.request_id,
                           decoded.value.connection_generation, timeout_ms);
  };
  auto mapping_result = platform::WindowsVirtualAsioSharedMemory::open(
      decoded.value.names.mapping);
  if (!mapping_result.ok()) {
    rollback();
    const auto& error = mapping_result.errors().front();
    return failure(error.code, error.message, error.native_error);
  }
  auto mapping = mapping_result.take_mapping();
  if (mapping->state() != platform::VirtualAsioSharedMemoryState::Ready ||
      !matches_header(mapping->header(), request, decoded.value)) {
    rollback();
    return failure("virtual_asio_shared_identity_mismatch",
                   "Shared transport identity did not match broker admission.");
  }

  auto events_result = platform::WindowsVirtualAsioEvents::open(
      decoded.value.names);
  if (!events_result.ok()) {
    rollback();
    const auto& error = events_result.errors().front();
    return failure(error.code, error.message, error.native_error);
  }
  auto input_result = platform::WindowsVirtualAsioSharedQueue::bind(
      *mapping, platform::VirtualAsioSharedQueueDirection::Input);
  auto output_result = platform::WindowsVirtualAsioSharedQueue::bind(
      *mapping, platform::VirtualAsioSharedQueueDirection::Output);
  if (!input_result.ok() || !output_result.ok()) {
    rollback();
    return failure("virtual_asio_shared_queue_bind_failed",
                   "Could not bind Virtual ASIO shared queues.");
  }

  return WindowsVirtualAsioBrokerClientConnectResult::success(
      std::unique_ptr<WindowsVirtualAsioBrokerClient>(
          new WindowsVirtualAsioBrokerClient(
              std::move(pipe_config), std::move(request.client_id),
              request.request_id, decoded.value.connection_generation,
              decoded.value.names, std::move(mapping),
              events_result.take_events(), input_result.take_queue(),
              output_result.take_queue())));
}

bool WindowsVirtualAsioBrokerClient::connected() const noexcept {
  return connected_;
}

std::uint64_t WindowsVirtualAsioBrokerClient::connection_generation()
    const noexcept {
  return connection_generation_;
}

const platform::WindowsVirtualAsioObjectNames&
WindowsVirtualAsioBrokerClient::names() const noexcept {
  return names_;
}

const platform::VirtualAsioSharedMemoryHeader&
WindowsVirtualAsioBrokerClient::header() const noexcept {
  return mapping_->header();
}

platform::VirtualAsioSharedQueueStatus
WindowsVirtualAsioBrokerClient::push_input(
    const realtime::AudioBuffer& source,
    const platform::VirtualAsioSharedBlockMetadata& metadata) noexcept {
  if (!connected_) {
    return platform::VirtualAsioSharedQueueStatus::NotReady;
  }
  return input_.push(source, metadata);
}

platform::VirtualAsioSharedQueueStatus
WindowsVirtualAsioBrokerClient::pop_output(
    realtime::AudioBuffer& destination,
    platform::VirtualAsioSharedBlockMetadata& metadata) noexcept {
  if (!connected_) {
    return platform::VirtualAsioSharedQueueStatus::NotReady;
  }
  return output_.pop(destination, metadata);
}

bool WindowsVirtualAsioBrokerClient::signal_input() noexcept {
  return connected_ && events_ != nullptr && events_->signal_input();
}

platform::WindowsVirtualAsioEventWaitResult
WindowsVirtualAsioBrokerClient::wait_output_or_shutdown(
    std::uint32_t timeout_ms) noexcept {
  if (!connected_ || events_ == nullptr) {
    return {platform::WindowsVirtualAsioEventWaitStatus::Failed,
            ERROR_INVALID_HANDLE};
  }
  return events_->wait_output_or_shutdown(timeout_ms);
}

NamedPipeControlResult WindowsVirtualAsioBrokerClient::disconnect(
    std::uint32_t timeout_ms) noexcept {
  if (!connected_) {
    return NamedPipeControlResult::success();
  }

  NamedPipeControlResult result = NamedPipeControlResult::failure({
      "virtual_asio_disconnect_failed",
      "Could not disconnect the Virtual ASIO broker client.",
  });
  try {
    const auto encoded = control::encode_virtual_asio_broker_disconnect({
        .request_id = request_id_ ^ 0x8000000000000000ULL,
        .client_id = client_id_,
        .connection_generation = connection_generation_,
    });
    if (encoded.ok()) {
      auto transaction = transact_named_pipe_control(
          pipe_config_, as_bytes(encoded.bytes), timeout_ms);
      if (transaction.ok()) {
        const auto decoded =
            control::decode_virtual_asio_broker_disconnect_response(
                as_u8(transaction.payload()));
        if (decoded.ok() && decoded.value.accepted) {
          result = NamedPipeControlResult::success();
        } else if (decoded.ok()) {
          result = NamedPipeControlResult::failure({
              decoded.value.error_code, decoded.value.error_message,
          });
        }
      } else {
        result = std::move(transaction);
      }
    }
  } catch (...) {
  }
  if (result.ok()) {
    close_local();
  }
  return result;
}

void WindowsVirtualAsioBrokerClient::close_local() noexcept {
  connected_ = false;
  if (events_ != nullptr) {
    events_->close();
  }
  if (mapping_ != nullptr) {
    mapping_->close();
  }
}

}  // namespace sar::service
