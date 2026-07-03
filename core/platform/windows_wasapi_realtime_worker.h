#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_graph_runner.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sar::platform {

struct WasapiRealtimeWorkerError {
  std::string code;
  std::string message;
};

struct WasapiRealtimeWorkerStats {
  std::uint64_t loop_cycles = 0;
  std::uint64_t graph_processed_cycles = 0;
  std::uint64_t idle_cycles = 0;
  std::uint64_t capture_idle_cycles = 0;
  std::uint64_t render_idle_cycles = 0;
  std::uint64_t wait_timeout_cycles = 0;
  std::uint64_t captured_frames = 0;
  std::uint64_t rendered_frames = 0;
};

class WasapiRealtimeWorkerResult {
 public:
  static WasapiRealtimeWorkerResult success();
  static WasapiRealtimeWorkerResult failure(std::vector<WasapiRealtimeWorkerError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<WasapiRealtimeWorkerError>& errors() const noexcept;

 private:
  explicit WasapiRealtimeWorkerResult(std::vector<WasapiRealtimeWorkerError> errors);

  std::vector<WasapiRealtimeWorkerError> errors_;
};

class WindowsWasapiRealtimeWorker {
 public:
  WindowsWasapiRealtimeWorker(WindowsWasapiGraphRunner& runner,
                              graph::Graph& graph,
                              diagnostics::EngineDiagnostics& diagnostics);
  WindowsWasapiRealtimeWorker(const WindowsWasapiRealtimeWorker&) = delete;
  WindowsWasapiRealtimeWorker& operator=(const WindowsWasapiRealtimeWorker&) = delete;
  ~WindowsWasapiRealtimeWorker();

  [[nodiscard]] WasapiRealtimeWorkerResult start(std::uint32_t timeout_ms);
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept;
  [[nodiscard]] std::uint64_t processed_cycles() const noexcept;
  [[nodiscard]] WasapiRealtimeWorkerStats stats() const noexcept;
  [[nodiscard]] std::vector<WasapiRealtimeWorkerError> last_errors() const;

 private:
  void run(std::uint32_t timeout_ms) noexcept;
  void set_errors(std::vector<WasapiRealtimeWorkerError> errors);

  WindowsWasapiGraphRunner& runner_;
  graph::Graph& graph_;
  diagnostics::EngineDiagnostics& diagnostics_;
  std::atomic_bool stop_requested_ = false;
  std::atomic_bool running_ = false;
  std::atomic_uint64_t loop_cycles_ = 0;
  std::atomic_uint64_t graph_processed_cycles_ = 0;
  std::atomic_uint64_t idle_cycles_ = 0;
  std::atomic_uint64_t capture_idle_cycles_ = 0;
  std::atomic_uint64_t render_idle_cycles_ = 0;
  std::atomic_uint64_t wait_timeout_cycles_ = 0;
  std::atomic_uint64_t captured_frames_ = 0;
  std::atomic_uint64_t rendered_frames_ = 0;
  mutable std::mutex errors_mutex_;
  std::vector<WasapiRealtimeWorkerError> last_errors_;
  std::thread worker_;
};

}  // namespace sar::platform
