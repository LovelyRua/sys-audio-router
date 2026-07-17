#pragma once

#include "core/control/control_session.h"
#include "core/control/control_wire_protocol.h"
#include "core/service/engine_audio_runtime.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace sar::service {

class EngineControlServiceCreateResult;

class EngineControlService {
 public:
  static EngineControlServiceCreateResult create(
      control::PresetDocument initial_preset,
      std::uint64_t initial_graph_version = 1);
  ~EngineControlService();

  [[nodiscard]] EngineAudioRuntimeResult install_audio_runtime(
      std::unique_ptr<EngineAudioRuntime> runtime);
  [[nodiscard]] EngineAudioRuntimeResult start_audio_runtime(
      std::uint32_t timeout_ms = 10);
  void stop_audio_runtime() noexcept;
  [[nodiscard]] bool has_audio_runtime() const noexcept;
  [[nodiscard]] bool audio_runtime_running() const noexcept;
  [[nodiscard]] diagnostics::EngineDiagnostics audio_runtime_diagnostics()
      const;

  [[nodiscard]] control::ControlWireEncodeResult handle_wire_request(
      std::span<const std::uint8_t> request);
  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

  [[nodiscard]] const control::ControlSession& session() const noexcept;

 private:
  explicit EngineControlService(
      std::unique_ptr<control::ControlSession> session) noexcept;

  std::unique_ptr<control::ControlSession> session_;
  std::unique_ptr<EngineAudioRuntime> audio_runtime_;
  mutable std::mutex control_mutex_;
};

class EngineControlServiceCreateResult {
 public:
  static EngineControlServiceCreateResult success(
      std::unique_ptr<EngineControlService> service);
  static EngineControlServiceCreateResult failure(
      std::vector<control::PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] EngineControlService& service() noexcept;
  [[nodiscard]] std::unique_ptr<EngineControlService> take_service() noexcept;
  [[nodiscard]] const std::vector<control::PresetError>& errors() const noexcept;

 private:
  EngineControlServiceCreateResult(
      std::unique_ptr<EngineControlService> service,
      std::vector<control::PresetError> errors) noexcept;

  std::unique_ptr<EngineControlService> service_;
  std::vector<control::PresetError> errors_;
};

}  // namespace sar::service
