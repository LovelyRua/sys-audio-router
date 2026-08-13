#include "core/platform/realtime_audio_fanout_sink.h"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace sar::platform {

RealtimeAudioFanoutSink::RealtimeAudioFanoutSink(
    std::vector<RealtimeAudioSink*> sinks)
    : sinks_(std::move(sinks)) {
  std::unordered_set<RealtimeAudioSink*> unique;
  for (auto* sink : sinks_) {
    if (sink == nullptr) {
      throw std::invalid_argument(
          "RealtimeAudioFanoutSink does not accept null sinks");
    }
    if (!unique.insert(sink).second) {
      throw std::invalid_argument(
          "RealtimeAudioFanoutSink does not accept duplicate sinks");
    }
  }
}

bool RealtimeAudioFanoutSink::write(
    const realtime::AudioBuffer& source) noexcept {
  std::uint64_t failures = 0;
  for (auto* sink : sinks_) {
    if (!sink->write(source)) {
      ++failures;
    }
  }
  published_blocks_.fetch_add(1, std::memory_order_relaxed);
  if (failures != 0) {
    partial_blocks_.fetch_add(1, std::memory_order_relaxed);
    failed_sink_writes_.fetch_add(failures, std::memory_order_relaxed);
  }
  return failures == 0;
}

RealtimeAudioFanoutSinkStats RealtimeAudioFanoutSink::stats() const noexcept {
  return {
      published_blocks_.load(std::memory_order_relaxed),
      partial_blocks_.load(std::memory_order_relaxed),
      failed_sink_writes_.load(std::memory_order_relaxed),
      sinks_.size(),
  };
}

std::span<RealtimeAudioSink* const> RealtimeAudioFanoutSink::sinks()
    const noexcept {
  return sinks_;
}

}  // namespace sar::platform
