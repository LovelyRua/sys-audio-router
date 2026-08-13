#include "core/platform/realtime_audio_input_assembler.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sar::platform {

RealtimeAudioInputAssembler::RealtimeAudioInputAssembler(
    std::size_t graph_channels,
    std::size_t frames_per_block,
    std::vector<RealtimeAudioInputBinding> bindings)
    : graph_channels_(graph_channels), frames_per_block_(frames_per_block) {
  if (graph_channels == 0 || frames_per_block == 0) {
    throw std::invalid_argument("Input assembler requires a graph shape");
  }
  std::vector<bool> occupied(graph_channels, false);
  bindings_.reserve(bindings.size());
  for (const auto& binding : bindings) {
    if (binding.source == nullptr || binding.channel_count == 0 ||
        binding.destination_first_channel > graph_channels ||
        binding.channel_count >
            graph_channels - binding.destination_first_channel) {
      throw std::invalid_argument("Invalid input assembler binding");
    }
    for (std::size_t channel = 0; channel < binding.channel_count; ++channel) {
      const auto index = binding.destination_first_channel + channel;
      if (occupied[index]) {
        throw std::invalid_argument("Input assembler bindings overlap");
      }
      occupied[index] = true;
    }
    bindings_.push_back({
        binding.source,
        binding.destination_first_channel,
        std::make_unique<realtime::AudioBuffer>(binding.channel_count,
                                                frames_per_block),
    });
  }
}

bool RealtimeAudioInputAssembler::read(
    realtime::AudioBuffer& destination) noexcept {
  destination.clear();
  if (destination.channels() != graph_channels_ ||
      destination.frames() != frames_per_block_) {
    return false;
  }

  bool any = false;
  for (auto& binding : bindings_) {
    binding.scratch->clear();
    if (!binding.source->read(*binding.scratch)) {
      continue;
    }
    any = true;
    for (std::size_t channel = 0; channel < binding.scratch->channels();
         ++channel) {
      const auto source = binding.scratch->channel(channel);
      auto target = destination.channel(
          binding.destination_first_channel + channel);
      std::copy(source.begin(), source.end(), target.begin());
    }
  }
  return any;
}

RealtimeAudioSourceDiagnostics RealtimeAudioInputAssembler::diagnostics()
    const noexcept {
  RealtimeAudioSourceDiagnostics aggregate;
  for (const auto& binding : bindings_) {
    const auto source = binding.source->diagnostics();
    aggregate.pushed_blocks += source.pushed_blocks;
    aggregate.dropped_blocks += source.dropped_blocks;
    aggregate.producer_underflows += source.producer_underflows;
    aggregate.producer_overflows += source.producer_overflows;
    aggregate.consumed_blocks += source.consumed_blocks;
    aggregate.mixed_blocks += source.mixed_blocks;
    aggregate.silent_reads += source.silent_reads;
    aggregate.clipped_samples += source.clipped_samples;
    aggregate.non_finite_samples += source.non_finite_samples;
    aggregate.maximum_queue_depth =
        std::max(aggregate.maximum_queue_depth, source.maximum_queue_depth);
    aggregate.active_producers += source.active_producers;
    aggregate.peak = std::max(aggregate.peak, source.peak);
  }
  return aggregate;
}

std::size_t RealtimeAudioInputAssembler::binding_count() const noexcept {
  return bindings_.size();
}

}  // namespace sar::platform
