#pragma once

#include "core/control/virtual_asio_broker_protocol.h"
#include "core/service/windows_named_pipe_control.h"
#include "core/service/windows_virtual_asio_transport_host.h"

#include <functional>
#include <memory>
#include <string>

namespace sar::service {

using WindowsVirtualAsioFormatProvider =
    std::function<platform::VirtualAsioFormat()>;

class WindowsVirtualAsioBrokerServer {
 public:
  WindowsVirtualAsioBrokerServer(
      std::wstring pipe_name,
      WindowsVirtualAsioTransportHost& host,
      WindowsVirtualAsioGraphFactory graph_factory,
      WindowsVirtualAsioFormatProvider format_provider = {});
  WindowsVirtualAsioBrokerServer(const WindowsVirtualAsioBrokerServer&) = delete;
  WindowsVirtualAsioBrokerServer& operator=(
      const WindowsVirtualAsioBrokerServer&) = delete;

  [[nodiscard]] NamedPipeControlResult start();
  void stop() noexcept;
  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] NamedPipeControlStats stats() const noexcept;
  [[nodiscard]] std::vector<NamedPipeControlError> last_errors() const;
  [[nodiscard]] const NamedPipeControlConfig& pipe_config() const noexcept;

 private:
  [[nodiscard]] NamedPipeControlResult handle(
      const NamedPipeControlPeer& peer,
      std::span<const std::byte> request);
  [[nodiscard]] NamedPipeControlResult handle_connect(
      const NamedPipeControlPeer& peer,
      std::span<const std::uint8_t> request);
  [[nodiscard]] NamedPipeControlResult handle_disconnect(
      const NamedPipeControlPeer& peer,
      std::span<const std::uint8_t> request);
  [[nodiscard]] NamedPipeControlResult handle_format(
      std::span<const std::uint8_t> request);

  WindowsVirtualAsioTransportHost& host_;
  WindowsVirtualAsioGraphFactory graph_factory_;
  WindowsVirtualAsioFormatProvider format_provider_;
  NamedPipeControlConfig pipe_config_;
  WindowsNamedPipeControlServer server_;
};

}  // namespace sar::service
