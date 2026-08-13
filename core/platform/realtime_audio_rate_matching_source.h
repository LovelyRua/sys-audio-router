#pragma once

#include "core/platform/realtime_audio_endpoint_queue.h"
#include "core/realtime/adaptive_resampler.h"
#include "core/realtime/fifo_waterline_controller.h"
#include "core/realtime/planar_audio_fifo.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sar::platform {

struct RealtimeAudioRateMatchingSourceStats {
  std::uint64_t successful_reads = 0;
  std::uint64_t silent_reads = 0;
  std::uint64_t input_overflow_frames = 0;
  std::uint64_t resampler_failures = 0;
  std::size_t input_fill_frames = 0;
  std::size_t maximum_input_fill_frames = 0;
  double correction_ppm = 0.0;
  double ratio = 1.0;
  bool primed = false;
};

// Converts a queued source clock into the destination graph clock.
// All storage and the libsamplerate state are prepared before audio starts.
class RealtimeAudioRateMatchingSource final : public RealtimeAudioSource {
 public:
  RealtimeAudioRateMatchingSource(RealtimeAudioEndpointQueue& upstream,
                                  std::uint32_t source_sample_rate,
                                  std::uint32_t destination_sample_rate,
                                  std::size_t latency_blocks = 4);
  RealtimeAudioRateMatchingSource(RealtimeAudioQueuedSource& upstream,
                                  std::size_t channels,
                                  std::size_t frames_per_block,
                                  std::uint32_t source_sample_rate,
                                  std::uint32_t destination_sample_rate,
                                  std::size_t latency_blocks = 4);

  [[nodiscard]] bool read(
      realtime::AudioBuffer& destination) noexcept override;
  [[nodiscard]] RealtimeAudioSourceDiagnostics diagnostics()
      const noexcept override;
  [[nodiscard]] RealtimeAudioRateMatchingSourceStats stats() const noexcept;

 private:
  void drain_upstream() noexcept;
  [[nodiscard]] bool generate_output() noexcept;

  RealtimeAudioQueuedSource& upstream_;
  std::size_t channels_;
  std::size_t block_frames_;
  std::uint32_t source_sample_rate_;
  std::uint32_t destination_sample_rate_;
  std::size_t target_fill_frames_;
  realtime::AudioBuffer ingestion_;
  realtime::AudioBuffer source_planar_;
  realtime::AudioBuffer generated_planar_;
  realtime::PlanarAudioFifo input_fifo_;
  realtime::PlanarAudioFifo output_fifo_;
  std::vector<float> source_interleaved_;
  std::vector<float> generated_interleaved_;
  realtime::AdaptiveResampler resampler_;
  realtime::FifoWaterlineController controller_;
  double nominal_ratio_ = 1.0;
  double ratio_ = 1.0;
  std::atomic_bool primed_ = false;
  std::atomic<std::uint64_t> successful_reads_ = 0;
  std::atomic<std::uint64_t> silent_reads_ = 0;
  std::atomic<std::uint64_t> input_overflow_frames_ = 0;
  std::atomic<std::uint64_t> resampler_failures_ = 0;
  std::atomic<std::size_t> input_fill_frames_ = 0;
  std::atomic<std::size_t> maximum_input_fill_frames_ = 0;
  std::atomic<std::uint64_t> correction_ppm_bits_ = 0;
  std::atomic<std::uint64_t> ratio_bits_ = 0;
};

}  // namespace sar::platform
