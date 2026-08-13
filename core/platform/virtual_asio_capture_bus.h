#pragma once

#include "core/platform/realtime_audio_source.h"
#include "core/platform/virtual_asio_client_registry.h"
#include "core/realtime/planar_audio_fifo.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace sar::platform {

struct VirtualAsioCaptureBusStats {
  std::uint64_t published_blocks = 0;
  std::uint64_t dropped_blocks = 0;
  std::uint64_t consumed_blocks = 0;
  std::uint64_t consumer_underflows = 0;
  std::uint64_t consumer_overflows = 0;
  std::size_t maximum_queue_depth = 0;
  std::size_t active_consumers = 0;
};

class VirtualAsioCaptureBus;

class VirtualAsioCaptureConsumer final : public RealtimeAudioQueuedSource {
 public:
  VirtualAsioCaptureConsumer() = default;
  VirtualAsioCaptureConsumer(const VirtualAsioCaptureConsumer&) = delete;
  VirtualAsioCaptureConsumer& operator=(const VirtualAsioCaptureConsumer&) = delete;
  VirtualAsioCaptureConsumer(VirtualAsioCaptureConsumer&& other) noexcept;
  VirtualAsioCaptureConsumer& operator=(
      VirtualAsioCaptureConsumer&& other) noexcept;
  ~VirtualAsioCaptureConsumer() override;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::size_t available_frames() const noexcept override;
  [[nodiscard]] bool read(realtime::AudioBuffer& destination) noexcept override;
  void reset() noexcept;

 private:
  friend class VirtualAsioCaptureBus;
  VirtualAsioCaptureConsumer(VirtualAsioCaptureBus* bus,
                             std::size_t slot,
                             std::uint64_t generation) noexcept;

  VirtualAsioCaptureBus* bus_ = nullptr;
  std::size_t slot_ = 0;
  std::uint64_t generation_ = 0;
};

class VirtualAsioCaptureBus final : public RealtimeAudioSink {
 public:
  VirtualAsioCaptureBus(std::size_t channels,
                        std::size_t producer_frames,
                        std::size_t maximum_consumers,
                        std::size_t queue_capacity_blocks);
  VirtualAsioCaptureBus(const VirtualAsioCaptureBus&) = delete;
  VirtualAsioCaptureBus& operator=(const VirtualAsioCaptureBus&) = delete;

  [[nodiscard]] VirtualAsioCaptureConsumer attach();
  [[nodiscard]] bool write(
      const realtime::AudioBuffer& source) noexcept override;
  [[nodiscard]] VirtualAsioCaptureBusStats stats() const noexcept;
  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t producer_frames() const noexcept;

 private:
  friend class VirtualAsioCaptureConsumer;

  struct Block {
    Block(std::size_t channels, std::size_t frames) : audio(channels, frames) {}
    realtime::AudioBuffer audio;
  };

  struct Slot {
    Slot(std::size_t channels,
         std::size_t producer_frames,
         std::size_t queue_capacity_blocks);

    std::vector<Block> blocks;
    realtime::PlanarAudioFifo pending;
    std::atomic<std::size_t> read_index = 0;
    std::atomic<std::size_t> write_index = 0;
    std::atomic<std::uint64_t> generation = 0;
    std::atomic<std::uint8_t> state = 0;
    std::atomic_bool producer_writing = false;
    std::atomic_bool consumer_reading = false;
  };

  [[nodiscard]] bool read(std::size_t slot,
                          std::uint64_t generation,
                          realtime::AudioBuffer& destination) noexcept;
  [[nodiscard]] std::size_t available_frames(
      std::size_t slot,
      std::uint64_t generation) const noexcept;
  void detach(std::size_t slot, std::uint64_t generation) noexcept;
  [[nodiscard]] std::size_t increment(const Slot& slot,
                                      std::size_t index) const noexcept;
  [[nodiscard]] std::size_t queue_depth(const Slot& slot,
                                        std::size_t read,
                                        std::size_t write) const noexcept;

  std::size_t channels_;
  std::size_t producer_frames_;
  std::vector<std::unique_ptr<Slot>> slots_;
  std::mutex attachment_mutex_;
  std::atomic<std::uint64_t> next_generation_ = 1;
  std::atomic<std::uint64_t> published_blocks_ = 0;
  std::atomic<std::uint64_t> dropped_blocks_ = 0;
  std::atomic<std::uint64_t> consumed_blocks_ = 0;
  std::atomic<std::uint64_t> consumer_underflows_ = 0;
  std::atomic<std::uint64_t> consumer_overflows_ = 0;
  std::atomic<std::size_t> maximum_queue_depth_ = 0;
  std::atomic<std::size_t> active_consumers_ = 0;
};

}  // namespace sar::platform
