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

}  // namespace sar::graph
