#pragma once

#include "core/graph/node.h"
#include "core/realtime/audio_buffer.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace sar::graph {

struct RouteEndpointDescriptor {
  std::string id;
  std::string label;
};

class RouteMatrix {
 public:
  RouteMatrix(std::size_t input_channels, std::size_t output_channels);
  RouteMatrix(std::vector<RouteEndpointDescriptor> inputs,
              std::vector<RouteEndpointDescriptor> outputs);

  [[nodiscard]] std::size_t input_channels() const noexcept;
  [[nodiscard]] std::size_t output_channels() const noexcept;
  [[nodiscard]] std::string_view input_id(std::size_t input_channel) const noexcept;
  [[nodiscard]] std::string_view input_label(std::size_t input_channel) const noexcept;
  [[nodiscard]] std::string_view output_id(std::size_t output_channel) const noexcept;
  [[nodiscard]] std::string_view output_label(std::size_t output_channel) const noexcept;

  void clear_routes() noexcept;
  void set_gain(std::size_t input_channel,
                std::size_t output_channel,
                float gain) noexcept;
  [[nodiscard]] float gain(std::size_t input_channel,
                           std::size_t output_channel) const noexcept;

  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) const noexcept;

 private:
  [[nodiscard]] std::size_t index(std::size_t input_channel,
                                  std::size_t output_channel) const noexcept;

  std::size_t input_channels_;
  std::size_t output_channels_;
  std::vector<RouteEndpointDescriptor> inputs_;
  std::vector<RouteEndpointDescriptor> outputs_;
  std::vector<float> gains_;
};

class RouteMatrixNode final : public Node {
 public:
  explicit RouteMatrixNode(RouteMatrix matrix) noexcept;

  [[nodiscard]] const RouteMatrix& matrix() const noexcept;

  void process(const realtime::ProcessContext& context,
               const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output) noexcept override;

 private:
  RouteMatrix matrix_;
};

}  // namespace sar::graph
