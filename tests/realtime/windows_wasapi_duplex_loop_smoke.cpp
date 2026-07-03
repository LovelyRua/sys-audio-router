#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_duplex_loop.h"

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

struct DefaultEndpointAvailability {
  bool capture = false;
  bool render = false;
};

DefaultEndpointAvailability default_endpoint_availability() {
  DefaultEndpointAvailability availability;
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return availability;
  }

  for (const auto& device : result.devices()) {
    if (!device.is_default) {
      continue;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Input) {
      availability.capture = true;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Output) {
      availability.render = true;
    }
  }
  return availability;
}

}  // namespace

int main() {
  const auto availability = default_endpoint_availability();
  if (!availability.capture || !availability.render) {
    std::cout << "Windows WASAPI duplex loop skipped: missing default endpoint\n";
    return 0;
  }

  sar::graph::Graph graph(22, 2, 128);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto result = sar::platform::open_default_wasapi_duplex_loop(graph, diagnostics);
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      if (error.code == "duplex_format_mismatch") {
        std::cout << "Windows WASAPI duplex loop skipped: " << error.message << '\n';
        return 0;
      }
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = result.take_loop();
  if (const auto failure = expect(loop->capture_probe().mix_format.channels ==
                                      loop->render_probe().mix_format.channels,
                                  "Expected matching duplex channel count")) {
    return failure;
  }
  if (const auto failure = expect(loop->capture_probe().buffer_frames ==
                                      loop->render_probe().buffer_frames,
                                  "Expected matching duplex buffer frames")) {
    return failure;
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

  if (const auto failure = expect(!loop->running(), "Expected stopped duplex loop")) {
    return failure;
  }
  if (const auto failure = expect(loop->last_errors().empty(),
                                  "Expected no duplex loop worker errors")) {
    return failure;
  }

  std::cout << "Windows WASAPI duplex loop smoke test passed\n";
  return 0;
}
