#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/platform/windows_asio_control_open.h"
#include "core/platform/windows_asio_host_events.h"

#include <atomic>
#include <cstdint>
#include <memory>

namespace sar::service {

enum class WindowsPhysicalAsioRuntimeError : std::uint8_t {
  None,
  InvalidRequest,
  ControlOpenFailed,
  GraphConfigurationMismatch,
  ResourceExhausted,
  StartFailed,
  StopFailed,
};

enum class WindowsPhysicalAsioRuntimeState : std::uint8_t {
  Ready,
  Running,
  Stopped,
};

struct WindowsPhysicalAsioRuntimeSummary {
  WindowsPhysicalAsioRuntimeState state = WindowsPhysicalAsioRuntimeState::Stopped;
  WindowsPhysicalAsioRuntimeError last_error =
      WindowsPhysicalAsioRuntimeError::None;
  platform::WindowsAsioControlOpenError control_open_error =
      platform::WindowsAsioControlOpenError::None;
  platform::WindowsAsioVendorHostError vendor_host_error =
      platform::WindowsAsioVendorHostError::None;
  double sample_rate = 0.0;
  std::uint32_t frames_per_block = 0;
  std::uint32_t input_channels = 0;
  std::uint32_t output_channels = 0;
  std::uint64_t rejected_callbacks = 0;
  diagnostics::EngineDiagnostics diagnostics;
  bool diagnostics_available = false;
};

class WindowsPhysicalAsioRuntime;

struct WindowsPhysicalAsioRuntimeOpenResult {
  std::unique_ptr<WindowsPhysicalAsioRuntime> runtime;
  WindowsPhysicalAsioRuntimeError error =
      WindowsPhysicalAsioRuntimeError::None;
  platform::WindowsAsioControlOpenError control_open_error =
      platform::WindowsAsioControlOpenError::None;

  [[nodiscard]] bool ok() const noexcept {
    return error == WindowsPhysicalAsioRuntimeError::None && runtime != nullptr;
  }
};

class WindowsPhysicalAsioRuntime {
 public:
  WindowsPhysicalAsioRuntime(const WindowsPhysicalAsioRuntime&) = delete;
  WindowsPhysicalAsioRuntime& operator=(const WindowsPhysicalAsioRuntime&) =
      delete;
  ~WindowsPhysicalAsioRuntime();

  [[nodiscard]] static WindowsPhysicalAsioRuntimeOpenResult open(
      std::unique_ptr<graph::Graph> graph,
      platform::WindowsAsioControlOpenRequest request,
      platform::WindowsAsioDriverActivator& activator,
      platform::WindowsAsioDriverNegotiator& negotiator) noexcept;

  [[nodiscard]] WindowsPhysicalAsioRuntimeError start() noexcept;
  [[nodiscard]] WindowsPhysicalAsioRuntimeError stop() noexcept;
  [[nodiscard]] WindowsPhysicalAsioRuntimeSummary summary() const noexcept;
  [[nodiscard]] platform::WindowsAsioHostEventSnapshot drain_host_events()
      noexcept;

 private:
  explicit WindowsPhysicalAsioRuntime(
      std::unique_ptr<graph::Graph> graph) noexcept;
  [[nodiscard]] static bool graph_callback(
      void* context, const realtime::AudioBuffer& input,
      realtime::AudioBuffer& output) noexcept;
  [[nodiscard]] bool process_graph(
      const realtime::AudioBuffer& input,
      realtime::AudioBuffer& output) noexcept;

  std::unique_ptr<graph::Graph> graph_;
  platform::WindowsAsioControlOpenResult control_open_;
  std::unique_ptr<realtime::AudioBuffer> graph_input_;
  std::unique_ptr<realtime::AudioBuffer> graph_output_;
  diagnostics::EngineDiagnostics diagnostics_;
  platform::WindowsAsioHostEvents host_events_;
  std::atomic<WindowsPhysicalAsioRuntimeState> state_{
      WindowsPhysicalAsioRuntimeState::Ready};
  std::atomic<std::uint64_t> rejected_callbacks_{0};
  std::atomic<WindowsPhysicalAsioRuntimeError> last_error_{
      WindowsPhysicalAsioRuntimeError::None};
  std::atomic<platform::WindowsAsioVendorHostError> vendor_host_error_{
      platform::WindowsAsioVendorHostError::None};
};

[[nodiscard]] const char* windows_physical_asio_runtime_error_name(
    WindowsPhysicalAsioRuntimeError error) noexcept;

}  // namespace sar::service
