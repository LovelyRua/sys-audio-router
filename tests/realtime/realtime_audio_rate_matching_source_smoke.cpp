#include "core/platform/realtime_audio_rate_matching_source.h"

#include <cassert>
#include <cmath>

int main() {
  constexpr std::size_t kFrames = 64;
  sar::platform::RealtimeAudioEndpointQueue queue(0, 2, kFrames, 12);
  sar::platform::RealtimeAudioRateMatchingSource source(queue, 48000, 48000,
                                                        3);
  sar::realtime::AudioBuffer block(2, kFrames);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    block.channel(0)[frame] = static_cast<float>(frame) / kFrames;
    block.channel(1)[frame] = -block.channel(0)[frame];
  }
  for (int index = 0; index < 5; ++index) {
    assert(queue.publisher().write(block));
  }

  sar::realtime::AudioBuffer output(2, kFrames);
  bool produced = false;
  for (int attempt = 0; attempt < 4 && !produced; ++attempt) {
    produced = source.read(output);
  }
  assert(produced);
  for (std::size_t frame = 0; frame < kFrames; ++frame) {
    assert(std::isfinite(output.channel(0)[frame]));
    assert(std::isfinite(output.channel(1)[frame]));
  }
  const auto stats = source.stats();
  assert(stats.primed);
  assert(stats.successful_reads == 1);
  assert(stats.resampler_failures == 0);
  assert(stats.maximum_input_fill_frames >= 3 * kFrames);
  assert(std::isfinite(stats.ratio));

  sar::realtime::AudioBuffer wrong_shape(1, kFrames);
  assert(!source.read(wrong_shape));
  return 0;
}
