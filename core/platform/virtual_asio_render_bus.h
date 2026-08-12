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

struct VirtualAsioRenderBusStats {
  std::uint64_t pushed_blocks = 0;
  std::uint64_t dropped_blocks = 0;
  // Per-active-producer starvation and capacity events seen at callback boundaries.
  std::uint64_t producer_underflows = 0;
  std::uint64_t producer_overflows = 0;
  std::uint64_t consumed_blocks = 0;
  std::uint64_t mixed_blocks = 0;
  std::uint64_t silent_reads = 0;
  std::uint64_t clipped_samples = 0;
  std::uint64_t non_finite_samples = 0;
  std::size_t maximum_queue_depth = 0;
  std::size_t active_producers = 0;
  float peak = 0.0F;
};

class VirtualAsioRenderBus;

class VirtualAsioRenderProducer {
 public:
  VirtualAsioRenderProducer() = default;
  VirtualAsioRenderProducer(const VirtualAsioRenderProducer&) = delete;
  VirtualAsioRenderProducer& operator=(const VirtualAsioRenderProducer&) = delete;
  VirtualAsioRenderProducer(VirtualAsioRenderProducer&& other) noexcept;
  VirtualAsioRenderProducer& operator=(VirtualAsioRenderProducer&& other) noexcept;
  ~VirtualAsioRenderProducer();

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] bool push(const realtime::AudioBuffer& source) noexcept;
  void reset() noexcept;

 private:
  friend class VirtualAsioRenderBus;
  VirtualAsioRenderProducer(VirtualAsioRenderBus* bus,
                            std::size_t slot,
                            std::uint64_t generation) noexcept;

  VirtualAsioRenderBus* bus_ = nullptr;
  std::size_t slot_ = 0;
  std::uint64_t generation_ = 0;
};

class VirtualAsioRenderBus final : public RealtimeAudioSource {
 public:
  VirtualAsioRenderBus(std::size_t channels,
                       std::size_t frames,
                       std::size_t maximum_producers,
                       std::size_t queue_capacity_blocks);
  VirtualAsioRenderBus(const VirtualAsioRenderBus&) = delete;
  VirtualAsioRenderBus& operator=(const VirtualAsioRenderBus&) = delete;

  [[nodiscard]] VirtualAsioRenderProducer attach();
  [[nodiscard]] bool read(realtime::AudioBuffer& destination) noexcept override;
  [[nodiscard]] RealtimeAudioSourceDiagnostics diagnostics()
      const noexcept override;
  [[nodiscard]] VirtualAsioRenderBusStats stats() const noexcept;
  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t frames() const noexcept;
  [[nodiscard]] bool accepts_consumer_format(
      std::size_t channels,
      std::size_t frames) const noexcept;

 private:
  friend class VirtualAsioRenderProducer;

  struct Block {
    explicit Block(std::size_t channels, std::size_t frames)
        : audio(channels, frames) {}
    realtime::AudioBuffer audio;
  };

  struct Slot {
    Slot(std::size_t channels,
         std::size_t frames,
         std::size_t queue_capacity_blocks);

    realtime::PlanarAudioFifo pending;
    realtime::AudioBuffer adapted_block;
    std::vector<Block> blocks;
    std::atomic<std::size_t> read_index = 0;
    std::atomic<std::size_t> write_index = 0;
    std::atomic<std::uint64_t> generation = 0;
    std::atomic<std::uint8_t> state = 0;
    // Starvation is meaningful only after this attachment has supplied audio.
    std::atomic_bool producer_started = false;
    std::atomic_bool consumer_reading = false;
  };

  [[nodiscard]] bool push(std::size_t slot,
                          std::uint64_t generation,
                          const realtime::AudioBuffer& source) noexcept;
  [[nodiscard]] bool enqueue(Slot& slot,
                             const realtime::AudioBuffer& source) noexcept;
  void drain_pending(Slot& slot) noexcept;
  void detach(std::size_t slot, std::uint64_t generation) noexcept;
  [[nodiscard]] std::size_t increment(const Slot& slot,
                                      std::size_t index) const noexcept;

  std::size_t channels_;
  std::size_t frames_;
  std::vector<std::unique_ptr<Slot>> slots_;
  std::mutex attachment_mutex_;
  std::atomic<std::uint64_t> next_generation_ = 1;
  std::atomic<std::uint64_t> pushed_blocks_ = 0;
  std::atomic<std::uint64_t> dropped_blocks_ = 0;
  std::atomic<std::uint64_t> producer_underflows_ = 0;
  std::atomic<std::uint64_t> producer_overflows_ = 0;
  std::atomic<std::uint64_t> consumed_blocks_ = 0;
  std::atomic<std::uint64_t> mixed_blocks_ = 0;
  std::atomic<std::uint64_t> silent_reads_ = 0;
  std::atomic<std::uint64_t> clipped_samples_ = 0;
  std::atomic<std::uint64_t> non_finite_samples_ = 0;
  std::atomic<std::size_t> maximum_queue_depth_ = 0;
  std::atomic<std::size_t> active_producers_ = 0;
  std::atomic<std::uint32_t> peak_bits_ = 0;
  mutable std::atomic<std::uint32_t> interval_peak_bits_ = 0;
};

}  // namespace sar::platform
