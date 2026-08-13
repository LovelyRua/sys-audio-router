#pragma once

#include "core/platform/realtime_audio_source.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sar::platform {

struct RealtimeAudioFanoutSinkStats {
  std::uint64_t published_blocks = 0;
  std::uint64_t partial_blocks = 0;
  std::uint64_t failed_sink_writes = 0;
  std::size_t sink_count = 0;
};

// Fans one graph result out to preconstructed endpoint queues. The sink list is
// immutable while audio is running; write() performs no allocation or locking.
class RealtimeAudioFanoutSink final : public RealtimeAudioSink {
 public:
  explicit RealtimeAudioFanoutSink(std::vector<RealtimeAudioSink*> sinks);

  [[nodiscard]] bool write(
      const realtime::AudioBuffer& source) noexcept override;
  [[nodiscard]] RealtimeAudioFanoutSinkStats stats() const noexcept;
  [[nodiscard]] std::span<RealtimeAudioSink* const> sinks() const noexcept;

 private:
  std::vector<RealtimeAudioSink*> sinks_;
  std::atomic<std::uint64_t> published_blocks_ = 0;
  std::atomic<std::uint64_t> partial_blocks_ = 0;
  std::atomic<std::uint64_t> failed_sink_writes_ = 0;
};

}  // namespace sar::platform
