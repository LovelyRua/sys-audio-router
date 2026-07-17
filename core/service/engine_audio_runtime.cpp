#include "core/service/engine_audio_runtime.h"

#include <utility>

namespace sar::service {

EngineAudioRuntimeResult EngineAudioRuntimeResult::success() {
  return EngineAudioRuntimeResult({});
}

EngineAudioRuntimeResult EngineAudioRuntimeResult::failure(
    std::vector<EngineAudioRuntimeError> errors) {
  return EngineAudioRuntimeResult(std::move(errors));
}

bool EngineAudioRuntimeResult::ok() const noexcept {
  return errors_.empty();
}

const std::vector<EngineAudioRuntimeError>& EngineAudioRuntimeResult::errors()
    const noexcept {
  return errors_;
}

EngineAudioRuntimeResult::EngineAudioRuntimeResult(
    std::vector<EngineAudioRuntimeError> errors) noexcept
    : errors_(std::move(errors)) {}

EngineAudioRuntimeBuildResult EngineAudioRuntimeBuildResult::success(
    std::unique_ptr<EngineAudioRuntime> runtime) {
  return {std::move(runtime), {}};
}

EngineAudioRuntimeBuildResult EngineAudioRuntimeBuildResult::failure(
    std::vector<EngineAudioRuntimeError> errors) {
  return {nullptr, std::move(errors)};
}

bool EngineAudioRuntimeBuildResult::ok() const noexcept {
  return runtime_ != nullptr && errors_.empty();
}

std::unique_ptr<EngineAudioRuntime>
EngineAudioRuntimeBuildResult::take_runtime() noexcept {
  return std::move(runtime_);
}

const std::vector<EngineAudioRuntimeError>&
EngineAudioRuntimeBuildResult::errors() const noexcept {
  return errors_;
}

EngineAudioRuntimeBuildResult::EngineAudioRuntimeBuildResult(
    std::unique_ptr<EngineAudioRuntime> runtime,
    std::vector<EngineAudioRuntimeError> errors) noexcept
    : runtime_(std::move(runtime)), errors_(std::move(errors)) {}

}  // namespace sar::service
