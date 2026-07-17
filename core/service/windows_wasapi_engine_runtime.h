#pragma once

#include "core/graph/graph.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/service/engine_audio_runtime.h"

#include <memory>
#include <string>
#include <vector>

namespace sar::service {

class WindowsWasapiEngineRuntimeOpenResult;

enum class WindowsWasapiEngineRuntimeMode {
  Render,
  Duplex,
};

class WindowsWasapiEngineRuntime final : public EngineAudioRuntime {
 public:
  WindowsWasapiEngineRuntime(const WindowsWasapiEngineRuntime&) = delete;
  WindowsWasapiEngineRuntime& operator=(const WindowsWasapiEngineRuntime&) = delete;
  ~WindowsWasapiEngineRuntime() override;

  [[nodiscard]] static WindowsWasapiEngineRuntimeOpenResult open_default_render(
      std::shared_ptr<graph::Graph> graph);
  [[nodiscard]] static WindowsWasapiEngineRuntimeOpenResult open_default_duplex(
      std::shared_ptr<graph::Graph> graph);
  [[nodiscard]] static WindowsWasapiEngineRuntimeOpenResult open_duplex(
      std::string capture_device_id,
      std::string render_device_id,
      std::shared_ptr<graph::Graph> graph);

  [[nodiscard]] EngineAudioRuntimeResult start(
      std::uint32_t timeout_ms) override;
  void stop() noexcept override;
  [[nodiscard]] bool running() const noexcept override;
  [[nodiscard]] std::uint64_t graph_version() const noexcept override;
  [[nodiscard]] diagnostics::EngineDiagnostics diagnostics() const override;
  [[nodiscard]] WindowsWasapiEngineRuntimeMode mode() const noexcept;
  [[nodiscard]] platform::WasapiRuntimeSummary runtime_summary() const;

 private:
  explicit WindowsWasapiEngineRuntime(
      std::shared_ptr<graph::Graph> graph) noexcept;

  std::shared_ptr<graph::Graph> graph_;
  diagnostics::EngineDiagnostics realtime_diagnostics_;
  std::unique_ptr<platform::WindowsWasapiRenderLoop> render_loop_;
  std::unique_ptr<platform::WindowsWasapiDuplexLoop> duplex_loop_;
};

class WindowsWasapiEngineRuntimeOpenResult {
 public:
  static WindowsWasapiEngineRuntimeOpenResult success(
      std::unique_ptr<WindowsWasapiEngineRuntime> runtime);
  static WindowsWasapiEngineRuntimeOpenResult failure(
      std::vector<EngineAudioRuntimeError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] std::unique_ptr<WindowsWasapiEngineRuntime> take_runtime() noexcept;
  [[nodiscard]] const std::vector<EngineAudioRuntimeError>& errors() const noexcept;

 private:
  WindowsWasapiEngineRuntimeOpenResult(
      std::unique_ptr<WindowsWasapiEngineRuntime> runtime,
      std::vector<EngineAudioRuntimeError> errors) noexcept;

  std::unique_ptr<WindowsWasapiEngineRuntime> runtime_;
  std::vector<EngineAudioRuntimeError> errors_;
};

}  // namespace sar::service
