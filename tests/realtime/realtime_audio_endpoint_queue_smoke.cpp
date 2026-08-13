#include "core/platform/realtime_audio_endpoint_queue.h"

#include <cassert>

int main() {
  sar::platform::RealtimeAudioEndpointQueue queue(2, 2, 4, 2);
  sar::realtime::AudioBuffer graph_output(6, 4);
  for (std::size_t channel = 0; channel < graph_output.channels(); ++channel) {
    for (std::size_t frame = 0; frame < graph_output.frames(); ++frame) {
      graph_output.channel(channel)[frame] =
          static_cast<float>(channel * 10 + frame);
    }
  }

  assert(queue.publisher().write(graph_output));
  sar::realtime::AudioBuffer endpoint_input(2, 4);
  assert(queue.consumer().read(endpoint_input));
  for (std::size_t frame = 0; frame < 4; ++frame) {
    assert(endpoint_input.channel(0)[frame] ==
           graph_output.channel(2)[frame]);
    assert(endpoint_input.channel(1)[frame] ==
           graph_output.channel(3)[frame]);
  }
  assert(!queue.consumer().read(endpoint_input));

  assert(queue.publisher().write(graph_output));
  assert(queue.publisher().write(graph_output));
  assert(!queue.publisher().write(graph_output));
  const auto stats = queue.stats();
  assert(stats.publisher.published_blocks == 4);
  assert(stats.publisher.downstream_failures == 1);
  assert(stats.queue.published_blocks == 3);
  assert(stats.queue.dropped_blocks == 1);
  assert(stats.queue.consumer_underflows == 1);
  assert(queue.graph_first_channel() == 2);
  assert(queue.endpoint_channels() == 2);
  return 0;
}
