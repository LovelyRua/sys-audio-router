#pragma once

#include "core/realtime/audio_buffer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sar::platform {

enum class WindowsAsioSampleEncoding : std::uint8_t {
  Int16Lsb, Int24Lsb, Int32Lsb, Int32Lsb16, Int32Lsb18, Int32Lsb20,
  Int32Lsb24, Float32Lsb, Float64Lsb,
};

struct WindowsAsioChannelBinding {
  WindowsAsioSampleEncoding encoding = WindowsAsioSampleEncoding::Float32Lsb;
  void* halves[2] = {};
};

struct WindowsAsioCallbackTransportConfig {
  std::size_t frames_per_block = 0;
  std::vector<WindowsAsioChannelBinding> inputs;
  std::vector<WindowsAsioChannelBinding> outputs;
};

enum class WindowsAsioCallbackStatus : std::uint8_t {
  Completed, InvalidBufferIndex, MissingBuffer, GraphRejected,
};

struct WindowsAsioCallbackTransportStats {
  std::uint64_t completed_cycles = 0;
  std::uint64_t invalid_buffer_indices = 0;
  std::uint64_t missing_buffers = 0;
  std::uint64_t graph_rejections = 0;
};

using WindowsAsioGraphProcess = bool (*)(
    void*, const realtime::AudioBuffer&, realtime::AudioBuffer&) noexcept;

struct WindowsAsioCallbackTransportOpenResult {
  std::unique_ptr<class WindowsAsioCallbackTransport> transport;
  [[nodiscard]] bool ok() const noexcept { return transport != nullptr; }
};

// Driver activation, createBuffers and lifecycle synchronization belong to the
// future vendor-host control plane. This object owns all callback-path storage.
class WindowsAsioCallbackTransport {
 public:
  WindowsAsioCallbackTransport(const WindowsAsioCallbackTransport&) = delete;
  WindowsAsioCallbackTransport& operator=(const WindowsAsioCallbackTransport&) = delete;

  [[nodiscard]] static WindowsAsioCallbackTransportOpenResult create(
      WindowsAsioCallbackTransportConfig, WindowsAsioGraphProcess,
      void* graph_context = nullptr) noexcept;
  [[nodiscard]] WindowsAsioCallbackStatus process(std::uint32_t) noexcept;
  [[nodiscard]] WindowsAsioCallbackTransportStats stats() const noexcept;
  [[nodiscard]] const realtime::AudioBuffer& graph_input() const noexcept;
  [[nodiscard]] const realtime::AudioBuffer& graph_output() const noexcept;

 private:
  WindowsAsioCallbackTransport(WindowsAsioCallbackTransportConfig,
                               WindowsAsioGraphProcess, void*);
  WindowsAsioCallbackTransportConfig config_;
  WindowsAsioGraphProcess graph_process_ = nullptr;
  void* graph_context_ = nullptr;
  realtime::AudioBuffer graph_input_;
  realtime::AudioBuffer graph_output_;
  std::atomic_uint64_t completed_cycles_ = 0;
  std::atomic_uint64_t invalid_buffer_indices_ = 0;
  std::atomic_uint64_t missing_buffers_ = 0;
  std::atomic_uint64_t graph_rejections_ = 0;
};

}  // namespace sar::platform
