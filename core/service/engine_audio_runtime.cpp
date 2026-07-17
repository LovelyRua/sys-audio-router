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

}  // namespace sar::service
