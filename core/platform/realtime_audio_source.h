#pragma once

#include "core/realtime/audio_buffer.h"

#include <cstddef>
#include <cstdint>

namespace sar::platform {

struct RealtimeAudioSourceDiagnostics {
  std::uint64_t pushed_blocks = 0;
  std::uint64_t dropped_blocks = 0;
  std::uint64_t consumed_blocks = 0;
  std::uint64_t mixed_blocks = 0;
  std::uint64_t silent_reads = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t non_finite_samples = 0;
  std::size_t maximum_queue_depth = 0;
  std::size_t active_producers = 0;
  float peak = 0.0F;
};

class RealtimeAudioSource {
 public:
  virtual ~RealtimeAudioSource() = default;

  [[nodiscard]] virtual bool read(
      realtime::AudioBuffer& destination) noexcept = 0;
  [[nodiscard]] virtual RealtimeAudioSourceDiagnostics diagnostics()
      const noexcept {
    return {};
  }
};

}  // namespace sar::platform
