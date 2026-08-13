#pragma once

#include "core/platform/realtime_audio_channel_slice_sink.h"
#include "core/platform/virtual_asio_capture_bus.h"

#include <cstddef>

namespace sar::platform {

struct RealtimeAudioEndpointQueueStats {
  RealtimeAudioChannelSliceSinkStats publisher;
  VirtualAsioCaptureBusStats queue;
};

// One preallocated SPSC bridge between the graph clock and an independently
// clocked endpoint worker. The graph publishes a channel slice; the endpoint
// consumes complete blocks at its own cadence.
class RealtimeAudioEndpointQueue {
 public:
  RealtimeAudioEndpointQueue(std::size_t graph_first_channel,
                             std::size_t endpoint_channels,
                             std::size_t frames_per_block,
                             std::size_t queue_capacity_blocks);
  RealtimeAudioEndpointQueue(const RealtimeAudioEndpointQueue&) = delete;
  RealtimeAudioEndpointQueue& operator=(
      const RealtimeAudioEndpointQueue&) = delete;

  [[nodiscard]] RealtimeAudioSink& publisher() noexcept;
  [[nodiscard]] RealtimeAudioSource& consumer() noexcept;
  [[nodiscard]] VirtualAsioCaptureConsumer& queued_consumer() noexcept;
  [[nodiscard]] RealtimeAudioEndpointQueueStats stats() const noexcept;
  [[nodiscard]] std::size_t graph_first_channel() const noexcept;
  [[nodiscard]] std::size_t endpoint_channels() const noexcept;
  [[nodiscard]] std::size_t frames_per_block() const noexcept;

 private:
  VirtualAsioCaptureBus queue_;
  VirtualAsioCaptureConsumer consumer_;
  RealtimeAudioChannelSliceSink publisher_;
};

}  // namespace sar::platform
