#pragma once

#include "core/realtime/audio_buffer.h"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sar::platform {

struct MockAsioBlockMetadata {
  std::uint64_t sequence = 0;
  std::uint64_t generation = 0;
};

struct MockAsioTransportStats {
  std::uint64_t dropped_input_blocks = 0;
  std::uint64_t dropped_output_blocks = 0;
  std::uint64_t input_underruns = 0;
  std::uint64_t output_underruns = 0;
  std::uint64_t input_sequence_discontinuities = 0;
  std::uint64_t output_sequence_discontinuities = 0;
};

// A fixed-format, bidirectional mock of the engine-side Virtual ASIO transport.
// The client is the sole producer of input and consumer of output. The engine is
// the sole consumer of input and producer of output.
class MockAsioTransport {
 public:
  MockAsioTransport(std::size_t channels,
                    std::size_t frames_per_block,
                    std::size_t queue_capacity_blocks);
  ~MockAsioTransport();

  MockAsioTransport(const MockAsioTransport&) = delete;
  MockAsioTransport& operator=(const MockAsioTransport&) = delete;
  MockAsioTransport(MockAsioTransport&&) = delete;
  MockAsioTransport& operator=(MockAsioTransport&&) = delete;

  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t frames_per_block() const noexcept;
  [[nodiscard]] std::size_t queue_capacity_blocks() const noexcept;
  [[nodiscard]] std::uint64_t connection_generation() const noexcept;

  // client -> engine
  [[nodiscard]] bool client_push_input(
      const realtime::AudioBuffer& block,
      std::uint64_t sequence) noexcept;
  [[nodiscard]] bool engine_pop_input(
      realtime::AudioBuffer& block,
      MockAsioBlockMetadata& metadata) noexcept;

  // engine -> client
  [[nodiscard]] bool engine_push_output(
      const realtime::AudioBuffer& block,
      std::uint64_t sequence) noexcept;
  [[nodiscard]] bool client_pop_output(
      realtime::AudioBuffer& block,
      MockAsioBlockMetadata& metadata) noexcept;

  [[nodiscard]] MockAsioTransportStats stats() const noexcept;

  // Control-plane only: callers must stop all four audio operations first.
  void reset_connection() noexcept;
  void reset_connection(std::uint64_t generation) noexcept;

 private:
  class Implementation;
  std::unique_ptr<Implementation> implementation_;
};

}  // namespace sar::platform
