#pragma once

#include "core/platform/virtual_asio_client_registry.h"
#include "core/platform/virtual_asio_capture_bus.h"
#include "core/platform/virtual_asio_render_bus.h"
#include "core/service/windows_virtual_asio_transport_session.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace sar::service {

using WindowsVirtualAsioGraphFactory =
    std::function<std::unique_ptr<graph::Graph>(
        const platform::VirtualAsioFormat&)>;

struct WindowsVirtualAsioHostConfig {
  std::string endpoint_token = "default";
  std::size_t maximum_clients = 8;
  std::uint32_t queue_capacity_blocks = 8;
  std::uint32_t wait_timeout_ms = 100;
};

struct WindowsVirtualAsioHostConnectRequest {
  platform::VirtualAsioClientRequest client;
  std::uint64_t client_nonce_low = 0;
  std::uint64_t client_nonce_high = 0;
};

struct WindowsVirtualAsioHostConnection {
  platform::VirtualAsioClientDescriptor client;
  platform::WindowsVirtualAsioObjectNames names;
  std::uint64_t server_nonce_low = 0;
  std::uint64_t server_nonce_high = 0;
};

struct WindowsVirtualAsioGraphRefreshResult {
  std::size_t updated_sessions = 0;
  std::vector<WindowsVirtualAsioTransportError> errors;

  [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

class WindowsVirtualAsioHostConnectResult {
 public:
  static WindowsVirtualAsioHostConnectResult success(
      WindowsVirtualAsioHostConnection connection);
  static WindowsVirtualAsioHostConnectResult failure(
      std::vector<WindowsVirtualAsioTransportError> errors);
  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const WindowsVirtualAsioHostConnection& connection() const noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioTransportError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioHostConnectResult(
      WindowsVirtualAsioHostConnection connection,
      std::vector<WindowsVirtualAsioTransportError> errors,
      bool succeeded);
  WindowsVirtualAsioHostConnection connection_;
  std::vector<WindowsVirtualAsioTransportError> errors_;
  bool succeeded_ = false;
};

class WindowsVirtualAsioTransportHost {
 public:
  explicit WindowsVirtualAsioTransportHost(
      WindowsVirtualAsioHostConfig config,
      platform::VirtualAsioRenderBus* render_bus = nullptr,
      platform::VirtualAsioCaptureBus* capture_bus = nullptr);
  WindowsVirtualAsioTransportHost(const WindowsVirtualAsioTransportHost&) = delete;
  WindowsVirtualAsioTransportHost& operator=(
      const WindowsVirtualAsioTransportHost&) = delete;
  ~WindowsVirtualAsioTransportHost();

  [[nodiscard]] WindowsVirtualAsioHostConnectResult connect(
      WindowsVirtualAsioHostConnectRequest request,
      std::unique_ptr<graph::Graph> graph,
      std::uint64_t graph_generation);
  [[nodiscard]] platform::VirtualAsioClientDisconnectResult disconnect(
      const std::string& client_id,
      std::uint64_t connection_generation);
  [[nodiscard]] std::size_t reap_stopped_sessions();
  [[nodiscard]] WindowsVirtualAsioGraphRefreshResult refresh_graphs(
      const WindowsVirtualAsioGraphFactory& graph_factory);
  void stop_all() noexcept;

  [[nodiscard]] std::vector<WindowsVirtualAsioHostConnection> connections() const;
  [[nodiscard]] std::size_t active_session_count() const noexcept;
  [[nodiscard]] std::uint64_t graph_generation() const noexcept;
  [[nodiscard]] const WindowsVirtualAsioHostConfig& config() const noexcept;

 private:
  struct SessionRecord {
    WindowsVirtualAsioHostConnection connection;
    std::unique_ptr<WindowsVirtualAsioTransportSession> session;
  };

  [[nodiscard]] std::size_t reap_stopped_sessions_locked();

  WindowsVirtualAsioHostConfig config_;
  platform::VirtualAsioClientRegistry registry_;
  platform::VirtualAsioRenderBus* render_bus_ = nullptr;
  platform::VirtualAsioCaptureBus* capture_bus_ = nullptr;
  mutable std::mutex mutex_;
  std::vector<SessionRecord> sessions_;
  std::uint64_t graph_generation_ = 1;
};

}  // namespace sar::service
