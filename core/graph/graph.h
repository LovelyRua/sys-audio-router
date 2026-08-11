#pragma once

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/node.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sar::graph {

class Graph {
 public:
  Graph(std::uint64_t version,
        std::size_t channels,
        std::size_t frames,
        std::uint32_t sample_rate = 48000);

  void add_node(std::unique_ptr<Node> node);
  void add_node(std::string label, std::unique_ptr<Node> node);
  void add_node(std::string id, std::string label, std::unique_ptr<Node> node);

  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

  [[nodiscard]] std::uint64_t version() const noexcept;
  [[nodiscard]] std::size_t channels() const noexcept;
  [[nodiscard]] std::size_t frames() const noexcept;
  [[nodiscard]] std::uint32_t sample_rate() const noexcept;
  [[nodiscard]] std::size_t node_count() const noexcept;
  [[nodiscard]] std::string_view node_id(std::size_t index) const noexcept;
  [[nodiscard]] std::string_view node_label(std::size_t index) const noexcept;
  [[nodiscard]] bool apply_realtime_parameters_from(
      const Graph& source) noexcept;

 private:
  std::uint64_t version_;
  std::uint32_t sample_rate_;
  std::size_t channels_;
  std::size_t frames_;
  realtime::AudioBuffer scratch_a_;
  realtime::AudioBuffer scratch_b_;
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<std::string> node_ids_;
  std::vector<std::string> node_labels_;
};

}  // namespace sar::graph
