#include "core/platform/realtime_audio_fanout_sink.h"

#include <cassert>
#include <stdexcept>

namespace {

class RecordingSink final : public sar::platform::RealtimeAudioSink {
 public:
  explicit RecordingSink(bool result = true) : result_(result) {}

  bool write(const sar::realtime::AudioBuffer& source) noexcept override {
    ++writes;
    sample = source.channel(0)[0];
    return result_;
  }

  std::size_t writes = 0;
  float sample = 0.0F;

 private:
  bool result_;
};

}  // namespace

int main() {
  RecordingSink first;
  RecordingSink second;
  RecordingSink failing(false);
  sar::realtime::AudioBuffer audio(2, 128);
  audio.channel(0)[0] = 0.75F;

  sar::platform::RealtimeAudioFanoutSink fanout({&first, &second});
  assert(fanout.write(audio));
  assert(first.writes == 1 && second.writes == 1);
  assert(first.sample == 0.75F && second.sample == 0.75F);
  assert(fanout.stats().published_blocks == 1);
  assert(fanout.stats().partial_blocks == 0);

  sar::platform::RealtimeAudioFanoutSink partial({&first, &failing, &second});
  assert(!partial.write(audio));
  assert(first.writes == 2 && second.writes == 2 && failing.writes == 1);
  assert(partial.stats().partial_blocks == 1);
  assert(partial.stats().failed_sink_writes == 1);

  bool rejected_null = false;
  try {
    sar::platform::RealtimeAudioFanoutSink invalid({nullptr});
  } catch (const std::invalid_argument&) {
    rejected_null = true;
  }
  assert(rejected_null);

  bool rejected_duplicate = false;
  try {
    sar::platform::RealtimeAudioFanoutSink invalid({&first, &first});
  } catch (const std::invalid_argument&) {
    rejected_duplicate = true;
  }
  assert(rejected_duplicate);
  return 0;
}
