#pragma once

#include "core/platform/realtime_audio_source.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace sar::platform {

struct RealtimeAudioChannelSliceSinkStats {
  std::uint64_t published_blocks = 0;
  std::uint64_t rejected_blocks = 0;
  std::uint64_t downstream_failures = 0;
};

// Extracts one immutable channel range before publishing to an endpoint queue.
// Scratch storage is allocated at construction; write() is lock-free and does
// no allocation.
class RealtimeAudioChannelSliceSink final : public RealtimeAudioSink {
 public:
  RealtimeAudioChannelSliceSink(std::size_t first_channel,
                                std::size_t channel_count,
                                std::size_t frames_per_block,
                                RealtimeAudioSink& downstream);

  [[nodiscard]] bool write(
      const realtime::AudioBuffer& source) noexcept override;
  [[nodiscard]] RealtimeAudioChannelSliceSinkStats stats() const noexcept;
  [[nodiscard]] std::size_t first_channel() const noexcept;
  [[nodiscard]] std::size_t channel_count() const noexcept;

 private:
  std::size_t first_channel_;
  realtime::AudioBuffer scratch_;
  RealtimeAudioSink& downstream_;
  std::atomic<std::uint64_t> published_blocks_ = 0;
  std::atomic<std::uint64_t> rejected_blocks_ = 0;
  std::atomic<std::uint64_t> downstream_failures_ = 0;
};

}  // namespace sar::platform
