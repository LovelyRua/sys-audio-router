#include "core/platform/realtime_audio_endpoint_queue.h"

#include <stdexcept>

namespace sar::platform {

RealtimeAudioEndpointQueue::RealtimeAudioEndpointQueue(
    std::size_t graph_first_channel,
    std::size_t endpoint_channels,
    std::size_t frames_per_block,
    std::size_t queue_capacity_blocks)
    : queue_(endpoint_channels, frames_per_block, 1, queue_capacity_blocks),
      consumer_(queue_.attach()),
      publisher_(graph_first_channel, endpoint_channels, frames_per_block,
                 queue_) {
  if (!consumer_.valid()) {
    throw std::runtime_error("Could not attach endpoint queue consumer");
  }
}

RealtimeAudioSink& RealtimeAudioEndpointQueue::publisher() noexcept {
  return publisher_;
}

RealtimeAudioSource& RealtimeAudioEndpointQueue::consumer() noexcept {
  return consumer_;
}

VirtualAsioCaptureConsumer&
RealtimeAudioEndpointQueue::queued_consumer() noexcept {
  return consumer_;
}

RealtimeAudioEndpointQueueStats RealtimeAudioEndpointQueue::stats()
    const noexcept {
  return {publisher_.stats(), queue_.stats()};
}

std::size_t RealtimeAudioEndpointQueue::graph_first_channel() const noexcept {
  return publisher_.first_channel();
}

std::size_t RealtimeAudioEndpointQueue::endpoint_channels() const noexcept {
  return publisher_.channel_count();
}

std::size_t RealtimeAudioEndpointQueue::frames_per_block() const noexcept {
  return queue_.producer_frames();
}

}  // namespace sar::platform
