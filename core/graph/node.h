#pragma once

#include "core/realtime/audio_buffer.h"
#include "core/realtime/process_context.h"

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

  void set_gain(float gain) noexcept;
  [[nodiscard]] float gain() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  float gain_;
};

}  // namespace sar::graph
