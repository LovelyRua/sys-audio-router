#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/node.h"

#include <memory>
#include <vector>

namespace sar::graph {

class Graph {
 public:
  Graph(std::uint64_t version,
        std::size_t channels,
        std::size_t frames,
        std::uint32_t sample_rate = 48000);

  void add_node(std::unique_ptr<Node> node);

  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

  [[nodiscard]] std::uint64_t version() const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept;

 private:
  std::uint64_t version_;
  std::uint32_t sample_rate_;
  realtime::AudioBuffer scratch_a_;
  realtime::AudioBuffer scratch_b_;
  std::vector<std::unique_ptr<Node>> nodes_;
};

}  // namespace sar::graph
