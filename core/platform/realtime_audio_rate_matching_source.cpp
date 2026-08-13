#include "core/platform/realtime_audio_rate_matching_source.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace sar::platform {

namespace {

constexpr std::size_t kFifoBlocks = 16;
constexpr std::size_t kMaximumDrainBlocksPerRead = 8;
constexpr std::size_t kMaximumResamplerPassesPerRead = 8;

void update_maximum(std::atomic<std::size_t>& target,
                    std::size_t value) noexcept {
  auto current = target.load(std::memory_order_relaxed);
  while (value > current &&
         !target.compare_exchange_weak(current, value,
                                       std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
  }
}

void interleave(const realtime::AudioBuffer& source,
                std::size_t frames,
                std::vector<float>& destination) noexcept {
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < source.channels(); ++channel) {
      destination[frame * source.channels() + channel] =
          source.channel(channel)[frame];
    }
  }
}

void deinterleave(const std::vector<float>& source,
                  std::size_t frames,
                  realtime::AudioBuffer& destination) noexcept {
  destination.clear();
  for (std::size_t frame = 0; frame < frames; ++frame) {
    for (std::size_t channel = 0; channel < destination.channels(); ++channel) {
      destination.channel(channel)[frame] =
          source[frame * destination.channels() + channel];
    }
  }
}

}  // namespace

RealtimeAudioRateMatchingSource::RealtimeAudioRateMatchingSource(
    RealtimeAudioEndpointQueue& upstream,
    std::uint32_t source_sample_rate,
    std::uint32_t destination_sample_rate,
    std::size_t latency_blocks)
    : RealtimeAudioRateMatchingSource(
          upstream.queued_consumer(), upstream.endpoint_channels(),
          upstream.frames_per_block(), source_sample_rate,
          destination_sample_rate, latency_blocks) {}

RealtimeAudioRateMatchingSource::RealtimeAudioRateMatchingSource(
    RealtimeAudioQueuedSource& upstream,
    std::size_t channels,
    std::size_t frames_per_block,
    std::uint32_t source_sample_rate,
    std::uint32_t destination_sample_rate,
    std::size_t latency_blocks)
    : upstream_(upstream),
      channels_(channels),
      block_frames_(frames_per_block),
      source_sample_rate_(source_sample_rate),
      destination_sample_rate_(destination_sample_rate),
      target_fill_frames_(latency_blocks * block_frames_),
      ingestion_(channels_, block_frames_),
      source_planar_(channels_, block_frames_ * 2),
      generated_planar_(channels_, block_frames_ * 2),
      input_fifo_(channels_, block_frames_ * kFifoBlocks),
      output_fifo_(channels_, block_frames_ * 4),
      source_interleaved_(channels_ * source_planar_.frames()),
      generated_interleaved_(channels_ * generated_planar_.frames()),
      controller_({
          .target_fill_frames = target_fill_frames_,
          .proportional_ppm_per_frame = 0.5,
          .integral_ppm_per_frame_second = 0.05,
          .maximum_correction_ppm = 500.0,
          .maximum_slew_ppm_per_second = 100.0,
      }) {
  if (channels == 0 || frames_per_block == 0 || source_sample_rate == 0 ||
      destination_sample_rate == 0 ||
      latency_blocks == 0 || latency_blocks >= kFifoBlocks) {
    throw std::invalid_argument("Invalid realtime rate matcher configuration");
  }
  nominal_ratio_ = static_cast<double>(destination_sample_rate_) /
                   static_cast<double>(source_sample_rate_);
  ratio_ = nominal_ratio_;
  ratio_bits_.store(std::bit_cast<std::uint64_t>(ratio_),
                    std::memory_order_relaxed);
  if (resampler_.initialize(channels_,
                            realtime::AdaptiveResamplerQuality::fastest) !=
      realtime::AdaptiveResamplerStatus::success) {
    throw std::runtime_error("Could not initialize endpoint rate matcher");
  }
}

bool RealtimeAudioRateMatchingSource::read(
    realtime::AudioBuffer& destination) noexcept {
  destination.clear();
  if (destination.channels() != channels_ ||
      destination.frames() != block_frames_) {
    silent_reads_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  drain_upstream();
  const auto fill = input_fifo_.available_frames();
  input_fill_frames_.store(fill, std::memory_order_relaxed);
  update_maximum(maximum_input_fill_frames_, fill);
  if (!primed_.load(std::memory_order_relaxed)) {
    if (fill < target_fill_frames_) {
      silent_reads_.fetch_add(1, std::memory_order_relaxed);
      return false;
    }
    primed_.store(true, std::memory_order_relaxed);
  }

  const auto elapsed_seconds =
      static_cast<double>(block_frames_) / destination_sample_rate_;
  const auto correction_ppm =
      controller_.update(static_cast<double>(fill), elapsed_seconds);
  ratio_ = nominal_ratio_ / (1.0 + correction_ppm * 0.000001);
  correction_ppm_bits_.store(std::bit_cast<std::uint64_t>(correction_ppm),
                             std::memory_order_relaxed);
  ratio_bits_.store(std::bit_cast<std::uint64_t>(ratio_),
                    std::memory_order_relaxed);

  for (std::size_t pass = 0;
       output_fifo_.available_frames() < block_frames_ &&
       pass < kMaximumResamplerPassesPerRead;
       ++pass) {
    if (!generate_output()) {
      break;
    }
  }
  if (output_fifo_.available_frames() < block_frames_) {
    silent_reads_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  static_cast<void>(output_fifo_.pop(destination, block_frames_));
  successful_reads_.fetch_add(1, std::memory_order_relaxed);
  return true;
}

void RealtimeAudioRateMatchingSource::drain_upstream() noexcept {
  for (std::size_t block = 0; block < kMaximumDrainBlocksPerRead; ++block) {
    if (upstream_.available_frames() < block_frames_) {
      break;
    }
    if (input_fifo_.free_frames() < block_frames_) {
      input_overflow_frames_.fetch_add(block_frames_,
                                       std::memory_order_relaxed);
      break;
    }
    if (!upstream_.read(ingestion_)) {
      break;
    }
    if (input_fifo_.push(ingestion_, block_frames_) != block_frames_) {
      input_overflow_frames_.fetch_add(block_frames_,
                                       std::memory_order_relaxed);
      break;
    }
  }
}

bool RealtimeAudioRateMatchingSource::generate_output() noexcept {
  const auto offered =
      std::min(input_fifo_.available_frames(), source_planar_.frames());
  static_cast<void>(input_fifo_.peek(source_planar_, offered));
  interleave(source_planar_, offered, source_interleaved_);
  const auto output_capacity = std::min(
      generated_planar_.frames(), output_fifo_.free_frames());
  if (output_capacity == 0) {
    return false;
  }
  const auto result = resampler_.process(
      std::span<const float>(source_interleaved_.data(), offered * channels_),
      static_cast<std::uint32_t>(offered), generated_interleaved_,
      static_cast<std::uint32_t>(output_capacity), ratio_);
  if (!result.ok()) {
    resampler_failures_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  static_cast<void>(input_fifo_.consume(result.input_frames_used));
  if (result.output_frames_generated != 0) {
    deinterleave(generated_interleaved_, result.output_frames_generated,
                 generated_planar_);
    static_cast<void>(output_fifo_.push(generated_planar_,
                                        result.output_frames_generated));
  }
  return result.input_frames_used != 0 || result.output_frames_generated != 0;
}

RealtimeAudioSourceDiagnostics RealtimeAudioRateMatchingSource::diagnostics()
    const noexcept {
  const auto snapshot = stats();
  auto result = upstream_.diagnostics();
  result.silent_reads += snapshot.silent_reads;
  return result;
}

RealtimeAudioRateMatchingSourceStats
RealtimeAudioRateMatchingSource::stats() const noexcept {
  return {
      successful_reads_.load(std::memory_order_relaxed),
      silent_reads_.load(std::memory_order_relaxed),
      input_overflow_frames_.load(std::memory_order_relaxed),
      resampler_failures_.load(std::memory_order_relaxed),
      input_fill_frames_.load(std::memory_order_relaxed),
      maximum_input_fill_frames_.load(std::memory_order_relaxed),
      std::bit_cast<double>(
          correction_ppm_bits_.load(std::memory_order_relaxed)),
      std::bit_cast<double>(ratio_bits_.load(std::memory_order_relaxed)),
      primed_.load(std::memory_order_relaxed),
  };
}

}  // namespace sar::platform
