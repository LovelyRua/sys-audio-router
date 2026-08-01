#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace sar::service {

struct NamedPipeControlError {
  std::string code;
  std::string message;
  std::uint32_t native_win32_code = 0;
};

struct NamedPipeControlConfig {
  std::wstring pipe_name = L"sys-audio-route-control";
  std::uint32_t maximum_message_bytes = 1024 * 1024;
  std::uint32_t request_timeout_ms = 2000;
};

struct NamedPipeControlStats {
  std::uint64_t accepted_connections = 0;
  std::uint64_t completed_requests = 0;
  std::uint64_t protocol_errors = 0;
  std::uint64_t handler_errors = 0;
};

struct NamedPipeControlPeer {
  std::uint32_t process_id = 0;
};

class NamedPipeControlResult {
 public:
  static NamedPipeControlResult success(std::vector<std::byte> payload = {});
  static NamedPipeControlResult failure(NamedPipeControlError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<std::byte>& payload() const noexcept;
  [[nodiscard]] std::vector<std::byte> take_payload() noexcept;
  [[nodiscard]] const NamedPipeControlError& error() const noexcept;

 private:
  NamedPipeControlResult(std::vector<std::byte> payload,
                         NamedPipeControlError error,
                         bool succeeded) noexcept;

  std::vector<std::byte> payload_;
  NamedPipeControlError error_;
  bool succeeded_ = false;
};

using NamedPipeControlHandler =
    std::function<NamedPipeControlResult(std::span<const std::byte>)>;
using NamedPipeControlPeerHandler = std::function<NamedPipeControlResult(
    const NamedPipeControlPeer&, std::span<const std::byte>)>;

class WindowsNamedPipeControlServer {
 public:
  WindowsNamedPipeControlServer(NamedPipeControlConfig config,
                                NamedPipeControlHandler handler);
  WindowsNamedPipeControlServer(NamedPipeControlConfig config,
                                NamedPipeControlPeerHandler handler);
  WindowsNamedPipeControlServer(const WindowsNamedPipeControlServer&) = delete;
  WindowsNamedPipeControlServer& operator=(const WindowsNamedPipeControlServer&) = delete;
  ~WindowsNamedPipeControlServer();

  [[nodiscard]] NamedPipeControlResult start();
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] NamedPipeControlStats stats() const noexcept;
  [[nodiscard]] std::vector<NamedPipeControlError> last_errors() const;

 private:
  struct ClientWorker {
    std::thread thread;
    std::shared_ptr<std::atomic_bool> finished;
  };

  void run() noexcept;
  void serve_client(void* pipe,
                    std::uint32_t client_process_id,
                    std::shared_ptr<std::atomic_bool> finished) noexcept;
  void reap_client_workers(bool join_all) noexcept;

  NamedPipeControlConfig config_;
  NamedPipeControlHandler handler_;
  NamedPipeControlPeerHandler peer_handler_;
  std::thread thread_;
  std::atomic_bool running_ = false;
  std::atomic_bool stop_requested_ = false;
  std::atomic_bool startup_complete_ = false;
  std::atomic_bool startup_succeeded_ = false;
  std::atomic<std::uint64_t> accepted_connections_ = 0;
  std::atomic<std::uint64_t> completed_requests_ = 0;
  std::atomic<std::uint64_t> protocol_errors_ = 0;
  std::atomic<std::uint64_t> handler_errors_ = 0;
  mutable std::atomic_flag error_lock_ = ATOMIC_FLAG_INIT;
  std::vector<NamedPipeControlError> last_errors_;
  std::mutex clients_mutex_;
  std::vector<ClientWorker> client_workers_;
  void* stop_event_ = nullptr;
};

[[nodiscard]] NamedPipeControlResult transact_named_pipe_control(
    const NamedPipeControlConfig& config,
    std::span<const std::byte> request,
    std::uint32_t timeout_ms = 2000);

}  // namespace sar::service
