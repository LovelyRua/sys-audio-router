#include "core/platform/windows_asio_callback_transport.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <utility>

namespace {
struct GraphState { std::uint64_t calls = 0; bool accept = true; };

bool graph(void* opaque, const sar::realtime::AudioBuffer& input,
           sar::realtime::AudioBuffer& output) noexcept {
  auto& state = *static_cast<GraphState*>(opaque);
  ++state.calls;
  if (!state.accept) return false;
  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    const auto source = input.channel(channel % input.channels());
    auto destination = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame)
      destination[frame] = source[frame] * 0.5F;
  }
  return true;
}

bool render_only_graph(void*, const sar::realtime::AudioBuffer& input,
                       sar::realtime::AudioBuffer& output) noexcept {
  assert(input.channels() == 0);
  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    auto destination = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame)
      destination[frame] = frame == 0 ? 1.5F : 0.25F;
  }
  return true;
}

bool capture_only_graph(void*, const sar::realtime::AudioBuffer& input,
                        sar::realtime::AudioBuffer& output) noexcept {
  assert(input.channels() == 1);
  assert(output.channels() == 0);
  return input.channel(0)[0] == 0.25F;
}

bool near(float actual, float expected) {
  return std::fabs(actual - expected) < 0.0001F;
}
}  // namespace

int main() {
  using namespace sar::platform;
  constexpr std::size_t frames = 4;
  std::array<float, frames> in_float_a{-1.0F, -0.5F, 0.5F, 1.0F};
  std::array<float, frames> in_float_b{0.25F, 0.5F, 0.75F, 1.0F};
  std::array<std::int16_t, frames> in_i16_a{-32768, -16384, 16384, 32767};
  std::array<std::int16_t, frames> in_i16_b{8192, 16384, 24576, 32767};
  std::array<float, frames> out_float_a{}, out_float_b{};
  std::array<std::int16_t, frames> out_i16_a{}, out_i16_b{};
  GraphState state;

  WindowsAsioCallbackTransportConfig config;
  config.frames_per_block = frames;
  config.inputs = {
      {WindowsAsioSampleEncoding::Float32Lsb,
       {in_float_a.data(), in_float_b.data()}},
      {WindowsAsioSampleEncoding::Int16Lsb,
       {in_i16_a.data(), in_i16_b.data()}},
  };
  config.outputs = {
      {WindowsAsioSampleEncoding::Float32Lsb,
       {out_float_a.data(), out_float_b.data()}},
      {WindowsAsioSampleEncoding::Int16Lsb,
       {out_i16_a.data(), out_i16_b.data()}},
  };
  auto opened = WindowsAsioCallbackTransport::create(
      std::move(config), graph, &state);
  assert(opened.ok());
  auto& transport = *opened.transport;

  assert(transport.process(0) == WindowsAsioCallbackStatus::Completed);
  assert(state.calls == 1);
  assert(near(out_float_a[0], -0.5F) && near(out_float_a[3], 0.5F));
  assert(out_i16_a[0] == -16384);
  assert(out_i16_a[3] >= 16382 && out_i16_a[3] <= 16384);

  assert(transport.process(1) == WindowsAsioCallbackStatus::Completed);
  assert(near(out_float_b[0], 0.125F));
  assert(out_i16_b[0] >= 4095 && out_i16_b[0] <= 4097);
  assert(transport.process(2) == WindowsAsioCallbackStatus::InvalidBufferIndex);

  state.accept = false;
  out_float_a.fill(123.0F);
  assert(transport.process(0) == WindowsAsioCallbackStatus::GraphRejected);
  assert(out_float_a[0] == 123.0F);
  const auto stats = transport.stats();
  assert(stats.completed_cycles == 2);
  assert(stats.invalid_buffer_indices == 1);
  assert(stats.graph_rejections == 1);

  WindowsAsioCallbackTransportConfig invalid;
  invalid.frames_per_block = frames;
  invalid.inputs.push_back({});
  invalid.outputs.push_back({});
  assert(!WindowsAsioCallbackTransport::create(
              std::move(invalid), graph, &state).ok());
  assert(!WindowsAsioCallbackTransport::create({}, graph, &state).ok());

  std::array<float, frames> render_a{}, render_b{};
  WindowsAsioCallbackTransportConfig render_only;
  render_only.frames_per_block = frames;
  render_only.outputs = {{WindowsAsioSampleEncoding::Float32Lsb,
                          {render_a.data(), render_b.data()}}};
  auto render_transport = WindowsAsioCallbackTransport::create(
      std::move(render_only), render_only_graph);
  assert(render_transport.ok());
  assert(render_transport.transport->process(0) ==
         WindowsAsioCallbackStatus::Completed);
  assert(render_a[0] == 1.5F);

  std::array<float, frames> capture_a{0.25F, 0.5F, 0.75F, 1.0F};
  std::array<float, frames> capture_b{};
  WindowsAsioCallbackTransportConfig capture_only;
  capture_only.frames_per_block = frames;
  capture_only.inputs = {{WindowsAsioSampleEncoding::Float32Lsb,
                          {capture_a.data(), capture_b.data()}}};
  auto capture_transport = WindowsAsioCallbackTransport::create(
      std::move(capture_only), capture_only_graph);
  assert(capture_transport.ok());
  assert(capture_transport.transport->process(0) ==
         WindowsAsioCallbackStatus::Completed);
}
