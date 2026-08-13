#pragma once

#include "core/platform/realtime_audio_source.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace sar::platform {

struct RealtimeAudioInputBinding {
  RealtimeAudioSource* source = nullptr;
  std::size_t destination_first_channel = 0;
  std::size_t channel_count = 0;
};

// Assembles independent endpoint sources into stable graph input ranges. The
// binding table and scratch buffers are immutable while audio is running.
class RealtimeAudioInputAssembler final : public RealtimeAudioSource {
 public:
  RealtimeAudioInputAssembler(std::size_t graph_channels,
                              std::size_t frames_per_block,
                              std::vector<RealtimeAudioInputBinding> bindings);

  [[nodiscard]] bool read(
      realtime::AudioBuffer& destination) noexcept override;
  [[nodiscard]] RealtimeAudioSourceDiagnostics diagnostics()
      const noexcept override;
  [[nodiscard]] std::size_t binding_count() const noexcept;

 private:
  struct OwnedBinding {
    RealtimeAudioSource* source = nullptr;
    std::size_t destination_first_channel = 0;
    std::unique_ptr<realtime::AudioBuffer> scratch;
  };

  std::size_t graph_channels_;
  std::size_t frames_per_block_;
  std::vector<OwnedBinding> bindings_;
};

}  // namespace sar::platform
