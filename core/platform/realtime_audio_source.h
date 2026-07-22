#pragma once

#include "core/realtime/audio_buffer.h"

namespace sar::platform {

class RealtimeAudioSource {
 public:
  virtual ~RealtimeAudioSource() = default;

  [[nodiscard]] virtual bool read(
      realtime::AudioBuffer& destination) noexcept = 0;
};

}  // namespace sar::platform
