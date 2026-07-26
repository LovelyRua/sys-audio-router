#include "core/service/windows_virtual_asio_transport_host.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <new>
#include <utility>

namespace sar::service {
namespace {

WindowsVirtualAsioHostConnectResult failure(std::string code,
                                             std::string message,
                                             std::uint32_t native_error = 0) {
  return WindowsVirtualAsioHostConnectResult::failure({
      {std::move(code), std::move(message), native_error},
  });
}

bool random_u64(std::uint64_t& value) noexcept {
  return BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                         BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
}

std::string random_token(std::uint64_t low, std::uint64_t high) {
  std::array<char, 33> text{};
  std::snprintf(text.data(), text.size(), "%016llx%016llx",
                static_cast<unsigned long long>(high),
                static_cast<unsigned long long>(low));
  return text.data();
}

}  // namespace

WindowsVirtualAsioHostConnectResult WindowsVirtualAsioHostConnectResult::success(
    WindowsVirtualAsioHostConnection connection) {
  return {std::move(connection), {}, true};
}

WindowsVirtualAsioHostConnectResult WindowsVirtualAsioHostConnectResult::failure(
    std::vector<WindowsVirtualAsioTransportError> errors) {
  return {{}, std::move(errors), false};
}

bool WindowsVirtualAsioHostConnectResult::ok() const noexcept {
  return succeeded_;
}

const WindowsVirtualAsioHostConnection&
WindowsVirtualAsioHostConnectResult::connection() const noexcept {
  return connection_;
}

const std::vector<WindowsVirtualAsioTransportError>&
WindowsVirtualAsioHostConnectResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioHostConnectResult::WindowsVirtualAsioHostConnectResult(
    WindowsVirtualAsioHostConnection connection,
    std::vector<WindowsVirtualAsioTransportError> errors,
    bool succeeded)
    : connection_(std::move(connection)),
      errors_(std::move(errors)),
      succeeded_(succeeded) {}

WindowsVirtualAsioTransportHost::WindowsVirtualAsioTransportHost(
    WindowsVirtualAsioHostConfig config,
    platform::VirtualAsioRenderBus* render_bus)
    : config_(std::move(config)),
      registry_(config_.maximum_clients),
      render_bus_(render_bus) {
  sessions_.reserve(config_.maximum_clients);
}

WindowsVirtualAsioTransportHost::~WindowsVirtualAsioTransportHost() {
  stop_all();
}

WindowsVirtualAsioHostConnectResult WindowsVirtualAsioTransportHost::connect(
    WindowsVirtualAsioHostConnectRequest request,
    std::unique_ptr<graph::Graph> graph) {
  std::lock_guard lock(mutex_);
  static_cast<void>(reap_stopped_sessions_locked());

  auto admitted = registry_.connect(request.client);
  if (!admitted.ok()) {
    std::vector<WindowsVirtualAsioTransportError> errors;
    for (const auto& error : admitted.errors()) {
      errors.push_back({error.code, error.message, 0});
    }
    return WindowsVirtualAsioHostConnectResult::failure(std::move(errors));
  }
  const auto client = admitted.client();
  const auto rollback = [&] {
    static_cast<void>(registry_.disconnect(
        client.client_id, client.connection_generation));
  };

  platform::VirtualAsioRenderProducer render_producer;
  if (render_bus_ != nullptr) {
    if (client.format.output_channels != render_bus_->channels() ||
        client.format.frames_per_block != render_bus_->frames()) {
      rollback();
      return failure("virtual_asio_render_bus_format_mismatch",
                     "Virtual ASIO client output does not match the render bus.");
    }
    render_producer = render_bus_->attach();
    if (!render_producer.valid()) {
      rollback();
      return failure("virtual_asio_render_bus_full",
                     "Virtual ASIO render bus has no free client slots.");
    }
  }

  std::uint64_t token_low = 0;
  std::uint64_t token_high = 0;
  std::uint64_t server_nonce_low = 0;
  std::uint64_t server_nonce_high = 0;
  if (!random_u64(token_low) || !random_u64(token_high) ||
      !random_u64(server_nonce_low) || !random_u64(server_nonce_high)) {
    rollback();
    return failure("virtual_asio_random_failed",
                   "Could not generate Virtual ASIO session identity.");
  }

  const auto names_result = platform::make_windows_virtual_asio_object_names(
      config_.endpoint_token, random_token(token_low, token_high),
      client.connection_generation);
  if (!names_result.ok()) {
    rollback();
    std::vector<WindowsVirtualAsioTransportError> errors;
    for (const auto& error : names_result.errors()) {
      errors.push_back({error.code, error.message, 0});
    }
    return WindowsVirtualAsioHostConnectResult::failure(std::move(errors));
  }

  const platform::VirtualAsioSharedMemoryConfig memory_config{
      .format = client.format,
      .queue_capacity_blocks = config_.queue_capacity_blocks,
  };
  const platform::VirtualAsioSharedMemoryIdentity identity{
      .connection_generation = client.connection_generation,
      .owner_process_id = GetCurrentProcessId(),
      .client_process_id = client.process_id,
      .server_nonce_low = server_nonce_low,
      .server_nonce_high = server_nonce_high,
      .client_nonce_low = request.client_nonce_low,
      .client_nonce_high = request.client_nonce_high,
  };
  auto session_result = WindowsVirtualAsioTransportSession::create(
      names_result.names(), memory_config, identity, std::move(graph),
      config_.wait_timeout_ms, std::move(render_producer));
  if (!session_result.ok()) {
    rollback();
    return WindowsVirtualAsioHostConnectResult::failure(
        session_result.errors());
  }
  auto session = session_result.take_session();
  if (!session->start()) {
    rollback();
    return failure("virtual_asio_session_start_failed",
                   "Could not start the Virtual ASIO transport session.");
  }

  WindowsVirtualAsioHostConnection connection{
      .client = client,
      .names = names_result.names(),
      .server_nonce_low = server_nonce_low,
      .server_nonce_high = server_nonce_high,
  };
  try {
    auto response_connection = connection;
    sessions_.push_back({std::move(connection), std::move(session)});
    return WindowsVirtualAsioHostConnectResult::success(
        std::move(response_connection));
  } catch (const std::bad_alloc&) {
    if (session != nullptr) {
      session->stop();
    }
    rollback();
    return failure("virtual_asio_host_allocation_failed",
                   "Could not retain the Virtual ASIO host session.");
  }
}

platform::VirtualAsioClientDisconnectResult
WindowsVirtualAsioTransportHost::disconnect(
    const std::string& client_id,
    std::uint64_t connection_generation) {
  std::lock_guard lock(mutex_);
  auto found = std::find_if(sessions_.begin(), sessions_.end(),
                            [&](const SessionRecord& record) {
    return record.connection.client.client_id == client_id &&
           record.connection.client.connection_generation ==
               connection_generation;
  });
  if (found == sessions_.end()) {
    return registry_.disconnect(client_id, connection_generation);
  }
  found->session->stop();
  auto result = registry_.disconnect(client_id, connection_generation);
  if (result.ok()) {
    sessions_.erase(found);
  }
  return result;
}

std::size_t WindowsVirtualAsioTransportHost::reap_stopped_sessions() {
  std::lock_guard lock(mutex_);
  return reap_stopped_sessions_locked();
}

WindowsVirtualAsioGraphRefreshResult
WindowsVirtualAsioTransportHost::refresh_graphs(
    const WindowsVirtualAsioGraphFactory& graph_factory) {
  if (!graph_factory) {
    return {
        .errors = {{"virtual_asio_graph_factory_missing",
                    "Virtual ASIO graph refresh requires a graph factory.", 0}},
    };
  }

  std::lock_guard lock(mutex_);
  static_cast<void>(reap_stopped_sessions_locked());
  std::vector<std::unique_ptr<graph::Graph>> replacements;
  try {
    replacements.reserve(sessions_.size());
    for (const auto& record : sessions_) {
      auto graph = graph_factory(record.connection.client.format);
      if (graph == nullptr) {
        return {
            .errors = {{"virtual_asio_graph_refresh_build_failed",
                        "Could not build a replacement graph for every "
                        "Virtual ASIO session.",
                        0}},
        };
      }
      if (!record.session->accepts_graph(*graph)) {
        return {
            .errors = {{"virtual_asio_graph_refresh_format_mismatch",
                        "A replacement graph does not match its Virtual ASIO "
                        "session format.",
                        0}},
        };
      }
      replacements.push_back(std::move(graph));
    }
  } catch (const std::bad_alloc&) {
    return {
        .errors = {{"virtual_asio_graph_refresh_allocation_failed",
                    "Could not allocate replacement Virtual ASIO graphs.", 0}},
    };
  } catch (...) {
    return {
        .errors = {{"virtual_asio_graph_refresh_factory_exception",
                    "The Virtual ASIO graph factory raised an exception.", 0}},
    };
  }

  for (std::size_t index = 0; index < sessions_.size(); ++index) {
    if (!sessions_[index].session->replace_graph(
            std::move(replacements[index]))) {
      return {
          .updated_sessions = index,
          .errors = {{"virtual_asio_graph_refresh_publish_failed",
                      "A validated Virtual ASIO graph could not be published.",
                      0}},
      };
    }
  }
  return {.updated_sessions = sessions_.size()};
}

void WindowsVirtualAsioTransportHost::stop_all() noexcept {
  std::lock_guard lock(mutex_);
  for (auto& record : sessions_) {
    record.session->stop();
    static_cast<void>(registry_.disconnect(
        record.connection.client.client_id,
        record.connection.client.connection_generation));
  }
  sessions_.clear();
}

std::vector<WindowsVirtualAsioHostConnection>
WindowsVirtualAsioTransportHost::connections() const {
  std::lock_guard lock(mutex_);
  std::vector<WindowsVirtualAsioHostConnection> result;
  result.reserve(sessions_.size());
  for (const auto& record : sessions_) {
    result.push_back(record.connection);
  }
  return result;
}

std::size_t WindowsVirtualAsioTransportHost::active_session_count()
    const noexcept {
  std::lock_guard lock(mutex_);
  return sessions_.size();
}

const WindowsVirtualAsioHostConfig& WindowsVirtualAsioTransportHost::config()
    const noexcept {
  return config_;
}

std::size_t WindowsVirtualAsioTransportHost::reap_stopped_sessions_locked() {
  std::size_t removed = 0;
  for (auto current = sessions_.begin(); current != sessions_.end();) {
    if (current->session->running()) {
      ++current;
      continue;
    }
    current->session->stop();
    static_cast<void>(registry_.disconnect(
        current->connection.client.client_id,
        current->connection.client.connection_generation));
    current = sessions_.erase(current);
    ++removed;
  }
  return removed;
}

}  // namespace sar::service
