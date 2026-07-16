#pragma once

#include "core/control/control_session.h"
#include "core/control/control_wire_protocol.h"

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
  std::mutex control_mutex_;
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
