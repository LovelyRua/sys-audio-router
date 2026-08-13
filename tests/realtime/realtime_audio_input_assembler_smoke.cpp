#include "core/platform/realtime_audio_input_assembler.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace {

class ConstantSource final : public sar::platform::RealtimeAudioSource {
 public:
  explicit ConstantSource(float value) : value_(value) {}
  bool read(sar::realtime::AudioBuffer& destination) noexcept override {
    for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
      std::fill(destination.channel(channel).begin(),
                destination.channel(channel).end(), value_ + channel);
    }
    return available;
  }
  bool available = true;

 private:
  float value_;
};

}  // namespace

int main() {
  ConstantSource asio(1.0F);
  ConstantSource capture(10.0F);
  sar::platform::RealtimeAudioInputAssembler assembler(
      6, 4, {{&asio, 0, 2}, {&capture, 4, 2}});
  sar::realtime::AudioBuffer output(6, 4);
  assert(assembler.read(output));
  for (std::size_t frame = 0; frame < 4; ++frame) {
    assert(output.channel(0)[frame] == 1.0F);
    assert(output.channel(1)[frame] == 2.0F);
    assert(output.channel(2)[frame] == 0.0F);
    assert(output.channel(3)[frame] == 0.0F);
    assert(output.channel(4)[frame] == 10.0F);
    assert(output.channel(5)[frame] == 11.0F);
  }
  capture.available = false;
  assert(assembler.read(output));
  assert(output.channel(4)[0] == 0.0F);
  assert(assembler.binding_count() == 2);

  bool overlap_rejected = false;
  try {
    sar::platform::RealtimeAudioInputAssembler invalid(
        4, 4, {{&asio, 0, 2}, {&capture, 1, 2}});
  } catch (const std::invalid_argument&) {
    overlap_rejected = true;
  }
  assert(overlap_rejected);
  return 0;
}
