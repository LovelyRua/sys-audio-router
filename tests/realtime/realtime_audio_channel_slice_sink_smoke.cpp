#include "core/platform/realtime_audio_channel_slice_sink.h"

#include <cassert>

namespace {

class RecordingSink final : public sar::platform::RealtimeAudioSink {
 public:
  RecordingSink() : received(2, 4) {}

  bool write(const sar::realtime::AudioBuffer& source) noexcept override {
    ++writes;
    received.copy_from(source);
    return accept;
  }

  sar::realtime::AudioBuffer received;
  std::size_t writes = 0;
  bool accept = true;
};

}  // namespace

int main() {
  RecordingSink downstream;
  sar::platform::RealtimeAudioChannelSliceSink slice(1, 2, 4, downstream);
  sar::realtime::AudioBuffer source(4, 4);
  for (std::size_t channel = 0; channel < source.channels(); ++channel) {
    for (std::size_t frame = 0; frame < source.frames(); ++frame) {
      source.channel(channel)[frame] =
          static_cast<float>(channel * 10 + frame);
    }
  }

  assert(slice.write(source));
  assert(downstream.writes == 1);
  for (std::size_t frame = 0; frame < 4; ++frame) {
    assert(downstream.received.channel(0)[frame] ==
           source.channel(1)[frame]);
    assert(downstream.received.channel(1)[frame] ==
           source.channel(2)[frame]);
  }

  sar::realtime::AudioBuffer too_narrow(2, 4);
  assert(!slice.write(too_narrow));
  downstream.accept = false;
  assert(!slice.write(source));
  const auto stats = slice.stats();
  assert(stats.published_blocks == 2);
  assert(stats.rejected_blocks == 1);
  assert(stats.downstream_failures == 1);
  assert(slice.first_channel() == 1);
  assert(slice.channel_count() == 2);
  return 0;
}
