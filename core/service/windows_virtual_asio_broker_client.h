#pragma once

#include "core/control/virtual_asio_broker_protocol.h"
#include "core/platform/windows_virtual_asio_events.h"
#include "core/platform/windows_virtual_asio_shared_queue.h"
#include "core/service/windows_named_pipe_control.h"

#include <memory>
#include <string>
#include <vector>

namespace sar::service {

struct WindowsVirtualAsioBrokerClientError {
  std::string code;
  std::string message;
  std::uint32_t native_error = 0;
};

class WindowsVirtualAsioBrokerClient;

class WindowsVirtualAsioBrokerClientConnectResult {
 public:
  static WindowsVirtualAsioBrokerClientConnectResult success(
      std::unique_ptr<WindowsVirtualAsioBrokerClient> client);
  static WindowsVirtualAsioBrokerClientConnectResult failure(
      std::vector<WindowsVirtualAsioBrokerClientError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioBrokerClient& client() noexcept;
  [[nodiscard]] std::unique_ptr<WindowsVirtualAsioBrokerClient> take_client()
      noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioBrokerClientError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioBrokerClientConnectResult(
      std::unique_ptr<WindowsVirtualAsioBrokerClient> client,
      std::vector<WindowsVirtualAsioBrokerClientError> errors);

  std::unique_ptr<WindowsVirtualAsioBrokerClient> client_;
  std::vector<WindowsVirtualAsioBrokerClientError> errors_;
};

class WindowsVirtualAsioBrokerClient {
 public:
  WindowsVirtualAsioBrokerClient(const WindowsVirtualAsioBrokerClient&) = delete;
  WindowsVirtualAsioBrokerClient& operator=(
      const WindowsVirtualAsioBrokerClient&) = delete;
  ~WindowsVirtualAsioBrokerClient();

  [[nodiscard]] static WindowsVirtualAsioBrokerClientConnectResult connect(
      NamedPipeControlConfig pipe_config,
      control::VirtualAsioBrokerConnectRequest request,
      std::uint32_t timeout_ms = 5000);

  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] std::uint64_t connection_generation() const noexcept;
  [[nodiscard]] const platform::WindowsVirtualAsioObjectNames& names()
      const noexcept;
  [[nodiscard]] const platform::VirtualAsioSharedMemoryHeader& header()
      const noexcept;

  [[nodiscard]] platform::VirtualAsioSharedQueueStatus push_input(
      const realtime::AudioBuffer& source,
      const platform::VirtualAsioSharedBlockMetadata& metadata) noexcept;
  [[nodiscard]] platform::VirtualAsioSharedQueueStatus pop_output(
      realtime::AudioBuffer& destination,
      platform::VirtualAsioSharedBlockMetadata& metadata) noexcept;
  [[nodiscard]] bool signal_input() noexcept;
  [[nodiscard]] platform::WindowsVirtualAsioEventWaitResult
  wait_output_or_shutdown(std::uint32_t timeout_ms) noexcept;

  [[nodiscard]] NamedPipeControlResult disconnect(
      std::uint32_t timeout_ms = 5000) noexcept;

 private:
  WindowsVirtualAsioBrokerClient(
      NamedPipeControlConfig pipe_config,
      std::string client_id,
      std::uint64_t request_id,
      std::uint64_t connection_generation,
      platform::WindowsVirtualAsioObjectNames names,
      std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping,
      std::unique_ptr<platform::WindowsVirtualAsioEvents> events,
      platform::WindowsVirtualAsioSharedQueue input,
      platform::WindowsVirtualAsioSharedQueue output) noexcept;
  void close_local() noexcept;

  NamedPipeControlConfig pipe_config_;
  std::string client_id_;
  std::uint64_t request_id_ = 0;
  std::uint64_t connection_generation_ = 0;
  platform::WindowsVirtualAsioObjectNames names_;
  std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping_;
  std::unique_ptr<platform::WindowsVirtualAsioEvents> events_;
  platform::WindowsVirtualAsioSharedQueue input_;
  platform::WindowsVirtualAsioSharedQueue output_;
  bool connected_ = false;
};

}  // namespace sar::service
