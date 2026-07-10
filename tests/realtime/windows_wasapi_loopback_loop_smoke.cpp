#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_loopback_loop.h"

#include "core/graph/node.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {
int expect(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; return 1; }
  return 0;
}

bool has_default_output_device() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) return false;
  for (const auto& device : result.devices()) {
    if (device.direction == sar::platform::AudioDeviceDirection::Output && device.is_default) return true;
  }
  return false;
}
}  // namespace

int main() {
  if (!has_default_output_device()) {
    std::cout << "Windows WASAPI loopback loop skipped: no default output endpoint\n";
    return 0;
  }
  const auto probe = sar::platform::probe_default_wasapi_stream(
      sar::platform::WasapiStreamDirection::Capture, sar::platform::WasapiStreamMode::Loopback);
  if (!probe.ok()) {
    std::cout << "Windows WASAPI loopback loop skipped: probe failed\n";
    return 0;
  }
  sar::graph::Graph graph(1, probe.probe().mix_format.channels, probe.probe().buffer_frames,
                          probe.probe().mix_format.sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto open = sar::platform::open_default_wasapi_loopback_loop(graph, diagnostics);
  if (!open.ok()) {
    for (const auto& error : open.errors()) std::cerr << error.code << ": " << error.message << '\n';
    return 1;
  }
  auto loop = open.take_loop();
  if (const auto failure = expect(loop->capture_diagnostics().mode == sar::platform::WasapiStreamMode::Loopback,
                                  "Expected loopback capture mode")) return failure;
  const auto start = loop->start(10);
  if (!start.ok()) {
    for (const auto& error : start.errors()) std::cerr << error.code << ": " << error.message << '\n';
    return 1;
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  loop->stop();
  const auto summary = loop->summary();
  if (const auto failure = expect(!summary.running, "Expected stopped loopback loop")) return failure;
  if (const auto failure = expect(summary.capture_stream.mode == sar::platform::WasapiStreamMode::Loopback,
                                  "Expected loopback summary mode")) return failure;
  if (const auto failure = expect(summary.runtime.has_capture_stream && !summary.runtime.has_render_stream,
                                  "Expected capture-only loopback runtime summary")) return failure;
  if (const auto failure = expect(summary.runtime.health != sar::platform::WasapiRuntimeHealth::Faulted,
                                  "Expected loopback runtime without fault")) return failure;
  std::cout << "Windows WASAPI loopback loop smoke test passed\n";
  return 0;
}
