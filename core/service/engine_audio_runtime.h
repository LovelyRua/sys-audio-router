#pragma once

#include "core/diagnostics/engine_diagnostics.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sar::service {

struct EngineAudioRuntimeError {
  std::string code;
  std::string message;
  std::optional<std::int32_t> native_hresult = std::nullopt;
  std::optional<std::uint32_t> native_win32_code = std::nullopt;
};

class EngineAudioRuntimeResult {
 public:
  static EngineAudioRuntimeResult success();
  static EngineAudioRuntimeResult failure(
      std::vector<EngineAudioRuntimeError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::vector<EngineAudioRuntimeError>& errors() const noexcept;

 private:
  explicit EngineAudioRuntimeResult(
      std::vector<EngineAudioRuntimeError> errors) noexcept;

  std::vector<EngineAudioRuntimeError> errors_;
};

class EngineAudioRuntime {
 public:
  EngineAudioRuntime(const EngineAudioRuntime&) = delete;
  EngineAudioRuntime& operator=(const EngineAudioRuntime&) = delete;
  virtual ~EngineAudioRuntime() = default;

  [[nodiscard]] virtual EngineAudioRuntimeResult start(
      std::uint32_t timeout_ms) = 0;
  virtual void stop() noexcept = 0;
  [[nodiscard]] virtual bool running() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t graph_version() const noexcept = 0;
  [[nodiscard]] virtual diagnostics::EngineDiagnostics diagnostics() const = 0;

 protected:
  EngineAudioRuntime() = default;
};

}  // namespace sar::service
