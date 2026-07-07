#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_render_loop.h"

#include "core/graph/node.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool has_default_output_device() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return false;
  }

  for (const auto& device : result.devices()) {
    if (device.direction == sar::platform::AudioDeviceDirection::Output &&
        device.is_default) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  if (!has_default_output_device()) {
    std::cout << "Windows WASAPI render loop skipped: no default output endpoint\n";
    return 0;
  }

  sar::graph::Graph graph(21, 2, 128);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto result = sar::platform::open_default_wasapi_render_loop(graph, diagnostics);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = result.take_loop();
  const auto render_diagnostics = loop->diagnostics();
  if (const auto failure = expect(render_diagnostics.state ==
                                      sar::platform::WasapiStreamState::Open,
                                  "Expected open render stream diagnostics")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.direction ==
                                      sar::platform::WasapiStreamDirection::Render,
                                  "Expected render stream diagnostics direction")) {
    return failure;
  }
  if (const auto failure = expect(render_diagnostics.buffer_frames ==
                                      loop->probe().buffer_frames,
                                  "Expected render diagnostics buffer size")) {
    return failure;
  }

  auto& input = loop->input_buffer();
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = 0.0F;
    }
  }

  const auto start_result = loop->start(10);
  if (!start_result.ok()) {
    for (const auto& error : start_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  loop->stop();

  if (const auto failure = expect(!loop->running(), "Expected stopped render loop")) {
    return failure;
  }
  if (const auto failure = expect(loop->last_errors().empty(),
                                  "Expected no render loop worker errors")) {
    return failure;
  }
  const auto stats = loop->stats();
  if (const auto failure = expect(stats.last_captured_frames == 0,
                                  "Expected render loop without last captured frames")) {
    return failure;
  }
  if (const auto failure = expect(stats.last_rendered_frames <= loop->probe().buffer_frames,
                                  "Expected bounded last rendered frames")) {
    return failure;
  }
  if (const auto failure = expect(stats.rendered_frames >= stats.last_rendered_frames,
                                  "Expected total rendered frames to cover last render")) {
    return failure;
  }

  std::cout << "Windows WASAPI render loop smoke test passed\n";
  return 0;
}
