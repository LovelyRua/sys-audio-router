#pragma once

#include "core/realtime/audio_buffer.h"
#include "core/realtime/atomic_float_parameter.h"
#include "core/realtime/process_context.h"

#include <atomic>
#include <cstdint>

namespace sar::graph {

class Node {
 public:
  virtual ~Node() = default;

  virtual void process(const realtime::ProcessContext& context,
                       const realtime::AudioBuffer& input,
                       realtime::AudioBuffer& output) noexcept = 0;
};

class PassthroughNode final : public Node {
 public:
  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;
};

class GainNode final : public Node {
 public:
  explicit GainNode(float gain) noexcept;

  [[nodiscard]] bool set_gain(float gain) noexcept;
  [[nodiscard]] float gain() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  realtime::AtomicFloatParameter gain_;
};

class MuteNode final : public Node {
 public:
  explicit MuteNode(bool muted = false) noexcept;

  void set_muted(bool muted) noexcept;
  [[nodiscard]] bool muted() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  static_assert(std::atomic_uint32_t::is_always_lock_free,
                "MuteNode requires a lock-free 32-bit atomic.");

  std::atomic_uint32_t muted_;
};

class MeterNode final : public Node {
 public:
  MeterNode() noexcept = default;

  void reset() noexcept;
  [[nodiscard]] float peak() const noexcept;
  [[nodiscard]] float rms() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  realtime::AtomicFloatParameter peak_;
  realtime::AtomicFloatParameter rms_;
};

// Phase 1 bus node. Mixes input channels into a fixed output channel count
// without allocation. When the output has fewer channels than the input, the
// extra input channels are summed into the available outputs (modulo the
// output channel count). When the output has more channels, the remaining
// outputs are silenced. This keeps the behavior deterministic and realtime-safe
// for the linear graph executor.
class BusNode final : public Node {
 public:
  explicit BusNode(std::size_t output_channels) noexcept;

  [[nodiscard]] std::size_t output_channels() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  std::size_t output_channels_;
};

// Phase 7 polarity invert node. Negates every sample on every channel. The
// invert state is atomic so the control plane can toggle it without locks.
class PolarityInvertNode final : public Node {
 public:
  explicit PolarityInvertNode(bool inverted = true) noexcept;

  void set_inverted(bool inverted) noexcept;
  [[nodiscard]] bool inverted() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  static_assert(std::atomic_uint32_t::is_always_lock_free,
                "PolarityInvertNode requires a lock-free 32-bit atomic.");

  std::atomic_uint32_t inverted_;
};

}  // namespace sar::graph
