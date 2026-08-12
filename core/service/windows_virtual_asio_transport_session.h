#pragma once

#include "core/graph/graph_snapshot.h"
#include "core/platform/virtual_asio_capture_bus.h"
#include "core/platform/virtual_asio_shared_memory_layout.h"
#include "core/platform/virtual_asio_render_bus.h"
#include "core/platform/windows_virtual_asio_events.h"
#include "core/platform/windows_virtual_asio_shared_queue.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sar::service {

struct WindowsVirtualAsioTransportError {
  std::string code;
  std::string message;
  std::uint32_t native_error = 0;
};

struct WindowsVirtualAsioTransportStats {
  bool running = false;
  std::uint64_t processed_blocks = 0;
  std::uint64_t dropped_output_blocks = 0;
  std::uint64_t input_queue_errors = 0;
  std::uint64_t output_queue_errors = 0;
  std::uint64_t wait_timeouts = 0;
  std::uint64_t wait_failures = 0;
  std::uint64_t output_signal_failures = 0;
  std::uint64_t realtime_thread_failures = 0;
  std::uint64_t client_process_exits = 0;
  std::uint64_t client_disconnects = 0;
  std::uint64_t last_sequence = 0;
  std::uint64_t dropped_render_bus_blocks = 0;
  std::uint64_t capture_bus_underflows = 0;
  std::uint64_t graph_updates = 0;
  std::uint64_t current_graph_version = 0;
};

class WindowsVirtualAsioTransportSession;

class WindowsVirtualAsioTransportSessionCreateResult {
 public:
  static WindowsVirtualAsioTransportSessionCreateResult success(
      std::unique_ptr<WindowsVirtualAsioTransportSession> session);
  static WindowsVirtualAsioTransportSessionCreateResult failure(
      std::vector<WindowsVirtualAsioTransportError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] WindowsVirtualAsioTransportSession& session() noexcept;
  [[nodiscard]] std::unique_ptr<WindowsVirtualAsioTransportSession>
  take_session() noexcept;
  [[nodiscard]] const std::vector<WindowsVirtualAsioTransportError>& errors()
      const noexcept;

 private:
  WindowsVirtualAsioTransportSessionCreateResult(
      std::unique_ptr<WindowsVirtualAsioTransportSession> session,
      std::vector<WindowsVirtualAsioTransportError> errors);

  std::unique_ptr<WindowsVirtualAsioTransportSession> session_;
  std::vector<WindowsVirtualAsioTransportError> errors_;
};

class WindowsVirtualAsioTransportSession {
 public:
  WindowsVirtualAsioTransportSession(
      const WindowsVirtualAsioTransportSession&) = delete;
  WindowsVirtualAsioTransportSession& operator=(
      const WindowsVirtualAsioTransportSession&) = delete;
  ~WindowsVirtualAsioTransportSession();

  [[nodiscard]] static WindowsVirtualAsioTransportSessionCreateResult create(
      platform::WindowsVirtualAsioObjectNames names,
      const platform::VirtualAsioSharedMemoryConfig& config,
      const platform::VirtualAsioSharedMemoryIdentity& identity,
      std::unique_ptr<graph::Graph> graph,
      std::uint32_t wait_timeout_ms = 100,
      platform::VirtualAsioRenderProducer render_producer = {},
      platform::VirtualAsioCaptureConsumer capture_consumer = {});

  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] const platform::WindowsVirtualAsioObjectNames& names()
      const noexcept;
  [[nodiscard]] platform::VirtualAsioSharedMemoryState shared_state()
      const noexcept;
  [[nodiscard]] WindowsVirtualAsioTransportStats stats() const noexcept;
  [[nodiscard]] bool accepts_graph(const graph::Graph& graph) const noexcept;
  [[nodiscard]] bool replace_graph(std::unique_ptr<graph::Graph> graph);

 private:
  WindowsVirtualAsioTransportSession(
      std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping,
      std::unique_ptr<platform::WindowsVirtualAsioEvents> events,
      platform::WindowsVirtualAsioSharedQueue input_queue,
      platform::WindowsVirtualAsioSharedQueue output_queue,
      std::shared_ptr<graph::Graph> graph,
      platform::VirtualAsioRenderProducer render_producer,
      platform::VirtualAsioCaptureConsumer capture_consumer,
      void* client_process_handle,
      std::uint32_t wait_timeout_ms);

  void run() noexcept;
  void process_available_input() noexcept;
  [[nodiscard]] bool client_process_exited() noexcept;

  std::unique_ptr<platform::WindowsVirtualAsioSharedMemory> mapping_;
  std::unique_ptr<platform::WindowsVirtualAsioEvents> events_;
  platform::WindowsVirtualAsioSharedQueue input_queue_;
  platform::WindowsVirtualAsioSharedQueue output_queue_;
  graph::GraphSnapshotPublisher graph_publisher_;
  platform::VirtualAsioRenderProducer render_producer_;
  platform::VirtualAsioCaptureConsumer capture_consumer_;
  realtime::AudioBuffer input_buffer_;
  realtime::AudioBuffer output_buffer_;
  std::uint32_t sample_rate_ = 0;
  diagnostics::EngineDiagnostics graph_diagnostics_;
  void* client_process_handle_ = nullptr;
  std::uint32_t wait_timeout_ms_ = 100;
  std::thread thread_;
  std::atomic_bool running_ = false;
  std::atomic_bool started_once_ = false;
  std::atomic<std::uint64_t> processed_blocks_ = 0;
  std::atomic<std::uint64_t> dropped_output_blocks_ = 0;
  std::atomic<std::uint64_t> input_queue_errors_ = 0;
  std::atomic<std::uint64_t> output_queue_errors_ = 0;
  std::atomic<std::uint64_t> wait_timeouts_ = 0;
  std::atomic<std::uint64_t> wait_failures_ = 0;
  std::atomic<std::uint64_t> output_signal_failures_ = 0;
  std::atomic<std::uint64_t> realtime_thread_failures_ = 0;
  std::atomic<std::uint64_t> client_process_exits_ = 0;
  std::atomic<std::uint64_t> client_disconnects_ = 0;
  std::atomic<std::uint64_t> last_sequence_ = 0;
  std::atomic<std::uint64_t> dropped_render_bus_blocks_ = 0;
  std::atomic<std::uint64_t> capture_bus_underflows_ = 0;
  std::atomic<std::uint64_t> graph_updates_ = 0;
  std::atomic<std::uint64_t> current_graph_version_ = 0;
};

}  // namespace sar::service
