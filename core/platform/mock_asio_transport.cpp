#include "core/platform/mock_asio_transport.h"

#include <atomic>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace sar::platform {
namespace {

class AudioBlockQueue {
 public:
  AudioBlockQueue(std::size_t channels,
                  std::size_t frames,
                  std::size_t capacity)
      : channels_(channels),
        frames_(frames),
        capacity_(capacity),
        slots_(capacity) {
    const auto samples_per_block = checked_sample_count(channels, frames);
    if (capacity == 0) {
      throw std::invalid_argument("MockAsioTransport capacity must be non-zero");
    }
    for (auto& slot : slots_) {
      slot.samples.resize(samples_per_block);
    }
  }

  [[nodiscard]] bool push(const realtime::AudioBuffer& block,
                          std::uint64_t sequence,
                          std::uint64_t generation) noexcept {
    if (!matches_format(block)) {
      return false;
    }

    const auto write = write_index_.load(std::memory_order_relaxed);
    const auto read = read_index_.load(std::memory_order_acquire);
    if (write - read >= capacity_) {
      return false;
    }

    auto& slot = slots_[write % capacity_];
    copy_to_slot(block, slot);
    slot.sequence = sequence;
    slot.generation = generation;
    write_index_.store(write + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool pop(realtime::AudioBuffer& block,
                         MockAsioBlockMetadata& metadata,
                         std::atomic_uint64_t& discontinuities) noexcept {
    if (!matches_format(block)) {
      return false;
    }

    const auto read = read_index_.load(std::memory_order_relaxed);
    if (read == write_index_.load(std::memory_order_acquire)) {
      return false;
    }

    const auto& slot = slots_[read % capacity_];
    copy_from_slot(slot, block);
    metadata = {.sequence = slot.sequence, .generation = slot.generation};
    if (slot.sequence != expected_sequence_) {
      discontinuities.fetch_add(1, std::memory_order_relaxed);
    }
    expected_sequence_ = slot.sequence + 1;
    read_index_.store(read + 1, std::memory_order_release);
    return true;
  }

  void reset() noexcept {
    read_index_.store(0, std::memory_order_relaxed);
    write_index_.store(0, std::memory_order_relaxed);
    expected_sequence_ = 0;
  }

 private:
  struct Slot {
    std::vector<float> samples;
    std::uint64_t sequence = 0;
    std::uint64_t generation = 0;
  };

  [[nodiscard]] static std::size_t checked_sample_count(std::size_t channels,
                                                         std::size_t frames) {
    if (channels == 0 || frames == 0 ||
        channels > std::numeric_limits<std::size_t>::max() / frames) {
      throw std::invalid_argument(
          "MockAsioTransport requires a valid non-zero fixed format");
    }
    return channels * frames;
  }

  [[nodiscard]] bool matches_format(
      const realtime::AudioBuffer& block) const noexcept {
    return block.channels() == channels_ && block.frames() == frames_;
  }

  void copy_to_slot(const realtime::AudioBuffer& block, Slot& slot) noexcept {
    for (std::size_t channel = 0; channel < channels_; ++channel) {
      const auto source = block.channel(channel);
      auto* destination = slot.samples.data() + (channel * frames_);
      for (std::size_t frame = 0; frame < frames_; ++frame) {
        destination[frame] = source[frame];
      }
    }
  }

  void copy_from_slot(const Slot& slot,
                      realtime::AudioBuffer& block) noexcept {
    for (std::size_t channel = 0; channel < channels_; ++channel) {
      const auto* source = slot.samples.data() + (channel * frames_);
      auto destination = block.channel(channel);
      for (std::size_t frame = 0; frame < frames_; ++frame) {
        destination[frame] = source[frame];
      }
    }
  }

  const std::size_t channels_;
  const std::size_t frames_;
  const std::uint64_t capacity_;
  std::vector<Slot> slots_;
  std::atomic_uint64_t read_index_ = 0;
  std::atomic_uint64_t write_index_ = 0;
  std::uint64_t expected_sequence_ = 0;
};

}  // namespace

class MockAsioTransport::Implementation {
 public:
  Implementation(std::size_t channels,
                 std::size_t frames,
                 std::size_t capacity)
      : channels(channels),
        frames(frames),
        capacity(capacity),
        client_to_engine(channels, frames, capacity),
        engine_to_client(channels, frames, capacity) {}

  const std::size_t channels;
  const std::size_t frames;
  const std::size_t capacity;
  AudioBlockQueue client_to_engine;
  AudioBlockQueue engine_to_client;
  std::atomic_uint64_t generation = 1;
  std::atomic_uint64_t dropped_input_blocks = 0;
  std::atomic_uint64_t dropped_output_blocks = 0;
  std::atomic_uint64_t input_underruns = 0;
  std::atomic_uint64_t output_underruns = 0;
  std::atomic_uint64_t input_sequence_discontinuities = 0;
  std::atomic_uint64_t output_sequence_discontinuities = 0;
};

MockAsioTransport::MockAsioTransport(std::size_t channels,
                                     std::size_t frames_per_block,
                                     std::size_t queue_capacity_blocks)
    : implementation_(std::make_unique<Implementation>(
          channels, frames_per_block, queue_capacity_blocks)) {}

MockAsioTransport::~MockAsioTransport() = default;

std::size_t MockAsioTransport::channels() const noexcept {
  return implementation_->channels;
}

std::size_t MockAsioTransport::frames_per_block() const noexcept {
  return implementation_->frames;
}

std::size_t MockAsioTransport::queue_capacity_blocks() const noexcept {
  return implementation_->capacity;
}

std::uint64_t MockAsioTransport::connection_generation() const noexcept {
  return implementation_->generation.load(std::memory_order_acquire);
}

bool MockAsioTransport::client_push_input(
    const realtime::AudioBuffer& block,
    std::uint64_t sequence) noexcept {
  if (implementation_->client_to_engine.push(
          block, sequence, connection_generation())) {
    return true;
  }
  if (block.channels() == channels() &&
      block.frames() == frames_per_block()) {
    implementation_->dropped_input_blocks.fetch_add(
        1, std::memory_order_relaxed);
  }
  return false;
}

bool MockAsioTransport::engine_pop_input(
    realtime::AudioBuffer& block,
    MockAsioBlockMetadata& metadata) noexcept {
  if (implementation_->client_to_engine.pop(
          block, metadata, implementation_->input_sequence_discontinuities)) {
    return true;
  }
  if (block.channels() == channels() &&
      block.frames() == frames_per_block()) {
    implementation_->input_underruns.fetch_add(1, std::memory_order_relaxed);
  }
  return false;
}

bool MockAsioTransport::engine_push_output(
    const realtime::AudioBuffer& block,
    std::uint64_t sequence) noexcept {
  if (implementation_->engine_to_client.push(
          block, sequence, connection_generation())) {
    return true;
  }
  if (block.channels() == channels() &&
      block.frames() == frames_per_block()) {
    implementation_->dropped_output_blocks.fetch_add(
        1, std::memory_order_relaxed);
  }
  return false;
}

bool MockAsioTransport::client_pop_output(
    realtime::AudioBuffer& block,
    MockAsioBlockMetadata& metadata) noexcept {
  if (implementation_->engine_to_client.pop(
          block, metadata, implementation_->output_sequence_discontinuities)) {
    return true;
  }
  if (block.channels() == channels() &&
      block.frames() == frames_per_block()) {
    implementation_->output_underruns.fetch_add(1, std::memory_order_relaxed);
  }
  return false;
}

MockAsioTransportStats MockAsioTransport::stats() const noexcept {
  return {
      .dropped_input_blocks =
          implementation_->dropped_input_blocks.load(std::memory_order_relaxed),
      .dropped_output_blocks =
          implementation_->dropped_output_blocks.load(std::memory_order_relaxed),
      .input_underruns =
          implementation_->input_underruns.load(std::memory_order_relaxed),
      .output_underruns =
          implementation_->output_underruns.load(std::memory_order_relaxed),
      .input_sequence_discontinuities =
          implementation_->input_sequence_discontinuities.load(
              std::memory_order_relaxed),
      .output_sequence_discontinuities =
          implementation_->output_sequence_discontinuities.load(
              std::memory_order_relaxed),
  };
}

void MockAsioTransport::reset_connection() noexcept {
  reset_connection(connection_generation() + 1);
}

void MockAsioTransport::reset_connection(std::uint64_t generation) noexcept {
  implementation_->client_to_engine.reset();
  implementation_->engine_to_client.reset();
  implementation_->generation.store(generation, std::memory_order_release);
}

}  // namespace sar::platform
