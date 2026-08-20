#include "core/platform/windows_asio_callback_transport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace sar::platform {
namespace {

float sanitize(float value) noexcept {
  return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
}

std::size_t sample_bytes(WindowsAsioSampleEncoding encoding) noexcept {
  if (encoding == WindowsAsioSampleEncoding::Int16Lsb) return 2;
  if (encoding == WindowsAsioSampleEncoding::Int24Lsb) return 3;
  if (encoding == WindowsAsioSampleEncoding::Float64Lsb) return 8;
  return 4;
}

unsigned valid_bits(WindowsAsioSampleEncoding encoding) noexcept {
  if (encoding == WindowsAsioSampleEncoding::Int32Lsb16) return 16;
  if (encoding == WindowsAsioSampleEncoding::Int32Lsb18) return 18;
  if (encoding == WindowsAsioSampleEncoding::Int32Lsb20) return 20;
  if (encoding == WindowsAsioSampleEncoding::Int32Lsb24) return 24;
  return 32;
}

float read_sample(const std::byte* source,
                  WindowsAsioSampleEncoding encoding) noexcept {
  if (encoding == WindowsAsioSampleEncoding::Float32Lsb) {
    float value{}; std::memcpy(&value, source, sizeof(value));
    return std::isfinite(value) ? value : 0.0F;
  }
  if (encoding == WindowsAsioSampleEncoding::Float64Lsb) {
    double value{}; std::memcpy(&value, source, sizeof(value));
    return std::isfinite(value) ? static_cast<float>(value) : 0.0F;
  }
  if (encoding == WindowsAsioSampleEncoding::Int16Lsb) {
    std::int16_t value{}; std::memcpy(&value, source, sizeof(value));
    return static_cast<float>(value) / 32768.0F;
  }
  if (encoding == WindowsAsioSampleEncoding::Int24Lsb) {
    std::uint32_t raw = std::to_integer<std::uint32_t>(source[0]) |
        (std::to_integer<std::uint32_t>(source[1]) << 8U) |
        (std::to_integer<std::uint32_t>(source[2]) << 16U);
    const auto value = static_cast<std::int32_t>(raw << 8U) >> 8U;
    return static_cast<float>(value) / 8388608.0F;
  }
  std::int32_t value{}; std::memcpy(&value, source, sizeof(value));
  return static_cast<float>(value) /
         static_cast<float>(std::uint64_t{1} << (valid_bits(encoding) - 1U));
}

void write_sample(std::byte* destination, WindowsAsioSampleEncoding encoding,
                  float input) noexcept {
  const auto value = sanitize(input);
  if (encoding == WindowsAsioSampleEncoding::Float32Lsb) {
    std::memcpy(destination, &value, sizeof(value)); return;
  }
  if (encoding == WindowsAsioSampleEncoding::Float64Lsb) {
    const double widened = value;
    std::memcpy(destination, &widened, sizeof(widened)); return;
  }
  if (encoding == WindowsAsioSampleEncoding::Int16Lsb) {
    const auto raw = static_cast<std::int16_t>(std::lrint(
        value * (value < 0.0F ? 32768.0F : 32767.0F)));
    std::memcpy(destination, &raw, sizeof(raw)); return;
  }
  if (encoding == WindowsAsioSampleEncoding::Int24Lsb) {
    const auto raw = static_cast<std::int32_t>(std::llround(
        value * (value < 0.0F ? 8388608.0 : 8388607.0)));
    destination[0] = static_cast<std::byte>(raw & 0xff);
    destination[1] = static_cast<std::byte>((raw >> 8) & 0xff);
    destination[2] = static_cast<std::byte>((raw >> 16) & 0xff);
    return;
  }
  const auto bits = valid_bits(encoding);
  const auto negative = static_cast<double>(std::uint64_t{1} << (bits - 1U));
  const auto raw = static_cast<std::int32_t>(std::llround(
      value * (value < 0.0F ? negative : negative - 1.0)));
  std::memcpy(destination, &raw, sizeof(raw));
}

}  // namespace

WindowsAsioCallbackTransportOpenResult WindowsAsioCallbackTransport::create(
    WindowsAsioCallbackTransportConfig config,
    WindowsAsioGraphProcess graph_process, void* graph_context) noexcept {
  if (config.frames_per_block == 0 || config.inputs.empty() ||
      config.outputs.empty() || graph_process == nullptr) return {};
  for (const auto& binding : config.inputs)
    if (!binding.halves[0] || !binding.halves[1]) return {};
  for (const auto& binding : config.outputs)
    if (!binding.halves[0] || !binding.halves[1]) return {};
  try {
    return {std::unique_ptr<WindowsAsioCallbackTransport>(
        new WindowsAsioCallbackTransport(std::move(config), graph_process,
                                         graph_context))};
  } catch (...) { return {}; }
}

WindowsAsioCallbackTransport::WindowsAsioCallbackTransport(
    WindowsAsioCallbackTransportConfig config,
    WindowsAsioGraphProcess graph_process, void* graph_context)
    : config_(std::move(config)), graph_process_(graph_process),
      graph_context_(graph_context),
      graph_input_(config_.inputs.size(), config_.frames_per_block),
      graph_output_(config_.outputs.size(), config_.frames_per_block) {}

WindowsAsioCallbackStatus WindowsAsioCallbackTransport::process(
    std::uint32_t buffer_index) noexcept {
  if (buffer_index > 1) {
    invalid_buffer_indices_.fetch_add(1, std::memory_order_relaxed);
    return WindowsAsioCallbackStatus::InvalidBufferIndex;
  }
  for (std::size_t channel = 0; channel < config_.inputs.size(); ++channel) {
    const auto& binding = config_.inputs[channel];
    if (!binding.halves[buffer_index]) {
      missing_buffers_.fetch_add(1, std::memory_order_relaxed);
      return WindowsAsioCallbackStatus::MissingBuffer;
    }
    const auto* source = static_cast<const std::byte*>(binding.halves[buffer_index]);
    const auto stride = sample_bytes(binding.encoding);
    auto planar = graph_input_.channel(channel);
    for (std::size_t frame = 0; frame < config_.frames_per_block; ++frame)
      planar[frame] = read_sample(source + frame * stride, binding.encoding);
  }
  graph_output_.clear();
  if (!graph_process_(graph_context_, graph_input_, graph_output_)) {
    graph_rejections_.fetch_add(1, std::memory_order_relaxed);
    return WindowsAsioCallbackStatus::GraphRejected;
  }
  for (std::size_t channel = 0; channel < config_.outputs.size(); ++channel) {
    const auto& binding = config_.outputs[channel];
    if (!binding.halves[buffer_index]) {
      missing_buffers_.fetch_add(1, std::memory_order_relaxed);
      return WindowsAsioCallbackStatus::MissingBuffer;
    }
    auto* destination = static_cast<std::byte*>(binding.halves[buffer_index]);
    const auto stride = sample_bytes(binding.encoding);
    const auto planar = graph_output_.channel(channel);
    for (std::size_t frame = 0; frame < config_.frames_per_block; ++frame)
      write_sample(destination + frame * stride, binding.encoding, planar[frame]);
  }
  completed_cycles_.fetch_add(1, std::memory_order_relaxed);
  return WindowsAsioCallbackStatus::Completed;
}

WindowsAsioCallbackTransportStats WindowsAsioCallbackTransport::stats() const noexcept {
  return {completed_cycles_.load(std::memory_order_relaxed),
          invalid_buffer_indices_.load(std::memory_order_relaxed),
          missing_buffers_.load(std::memory_order_relaxed),
          graph_rejections_.load(std::memory_order_relaxed)};
}

const realtime::AudioBuffer& WindowsAsioCallbackTransport::graph_input() const noexcept { return graph_input_; }
const realtime::AudioBuffer& WindowsAsioCallbackTransport::graph_output() const noexcept { return graph_output_; }

}  // namespace sar::platform
