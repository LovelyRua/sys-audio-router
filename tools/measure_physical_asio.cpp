#include "tools/physical_asio_measure_options.h"

#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_asio_control_open.h"
#include "core/platform/windows_asio_device_provider.h"
#include "core/platform/windows_asio_driver_probe.h"
#include "core/service/windows_physical_asio_runtime.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

class ComApartment {
 public:
  ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() {
    if (SUCCEEDED(result_)) CoUninitialize();
  }
  [[nodiscard]] bool ok() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }

 private:
  HRESULT result_ = E_FAIL;
};

std::string lowercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

std::string strip_asio_prefix(std::string id) {
  constexpr auto prefix = "asio:";
  if (id.starts_with(prefix)) id.erase(0, std::char_traits<char>::length(prefix));
  return id;
}

const sar::platform::AudioDeviceDescriptor* select_driver(
    const std::vector<sar::platform::AudioDeviceDescriptor>& devices,
    const std::string& selector, bool& ambiguous) {
  const auto wanted = lowercase(selector);
  const sar::platform::AudioDeviceDescriptor* selected = nullptr;
  for (const auto& device : devices) {
    const auto clsid = strip_asio_prefix(lowercase(device.id));
    if (lowercase(device.label) != wanted && lowercase(device.id) != wanted &&
        clsid != wanted) {
      continue;
    }
    if (selected) {
      ambiguous = true;
      return nullptr;
    }
    selected = &device;
  }
  return selected;
}

const char* runtime_state_name(
    sar::service::WindowsPhysicalAsioRuntimeState state) noexcept {
  using State = sar::service::WindowsPhysicalAsioRuntimeState;
  switch (state) {
    case State::Ready: return "ready";
    case State::Running: return "running";
    case State::Stopped: return "stopped";
  }
  return "unknown";
}

void print_summary(
    const sar::service::WindowsPhysicalAsioRuntimeSummary& summary,
    const sar::platform::WindowsAsioHostEventSnapshot& events) {
  std::cout << "runtime state=" << runtime_state_name(summary.state)
            << " error="
            << sar::service::windows_physical_asio_runtime_error_name(
                   summary.last_error)
            << " rejected_callbacks=" << summary.rejected_callbacks << '\n';
  std::cout << "callbacks processed_blocks="
            << summary.diagnostics.processed_blocks
            << " xruns=" << summary.diagnostics.xrun_count
            << " peak_callback_us="
            << summary.diagnostics.peak_callback_seconds * 1000000.0 << '\n';
  std::cout << "host_events reset=" << events.reset_requests
            << " buffer_size_change=" << events.buffer_size_changes
            << " latency_change=" << events.latencies_changed
            << " resync=" << events.resync_requests
            << " latest_buffer_frames=" << events.latest_buffer_size << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<const char*> argument_view;
  argument_view.reserve(static_cast<std::size_t>(argc));
  for (int index = 0; index < argc; ++index) argument_view.push_back(argv[index]);
  const auto parsed = sar::tools::parse_physical_asio_measure_options(
      argc, argument_view.data());
  if (!parsed.ok()) {
    std::cerr << "error: " << parsed.error << '\n'
              << sar::tools::physical_asio_measure_usage();
    return 2;
  }
  if (parsed.options.help) {
    std::cout << sar::tools::physical_asio_measure_usage();
    return 0;
  }

  ComApartment com;
  if (!com.ok()) {
    std::cerr << "error: COM initialization failed\n";
    return 3;
  }

  sar::platform::WindowsAsioDeviceProvider provider;
  const auto listed = provider.list_devices();
  if (!listed.ok()) {
    for (const auto& error : listed.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 4;
  }
  bool ambiguous = false;
  const auto* device = select_driver(listed.devices(), parsed.options.driver,
                                     ambiguous);
  if (!device) {
    std::cerr << "error: ASIO driver "
              << (ambiguous ? "selector is ambiguous: " : "not found: ")
              << parsed.options.driver << '\n';
    for (const auto& candidate : listed.devices()) {
      std::cerr << "  " << candidate.label << " " << candidate.id << '\n';
    }
    return 5;
  }

  const auto clsid = strip_asio_prefix(device->id);
  const auto probed = sar::platform::probe_windows_asio_driver(clsid);
  if (!probed.ok()) {
    std::cerr << probed.error().code << ": " << probed.error().message << '\n';
    return 6;
  }
  const auto& probe = probed.probe();
  const auto block_frames = parsed.options.block_frames != 0
                                ? parsed.options.block_frames
                                : probe.preferred_buffer_frames;
  const auto channels = std::max(probe.input_channels, probe.output_channels);
  if (block_frames == 0 || channels == 0) {
    std::cerr << "error: driver reported an unusable channel or buffer shape\n";
    return 7;
  }

  auto graph = std::make_unique<sar::graph::Graph>(
      1, channels, block_frames, parsed.options.sample_rate);
  graph->add_node(std::make_unique<sar::graph::PassthroughNode>());

  auto activator = sar::platform::make_windows_asio_driver_activator();
  auto negotiator = sar::platform::make_windows_asio_driver_negotiator();
  if (!activator || !negotiator) {
    std::cerr << "error: Physical ASIO platform services unavailable\n";
    return 8;
  }
  sar::platform::WindowsAsioControlOpenRequest request;
  request.driver = probe;
  request.sample_rate = parsed.options.sample_rate;
  request.preferred_block_frames = block_frames;

  auto opened = sar::service::WindowsPhysicalAsioRuntime::open(
      std::move(graph), request, *activator, *negotiator);
  if (!opened.ok()) {
    std::cerr << "open_failed runtime="
              << sar::service::windows_physical_asio_runtime_error_name(
                     opened.error)
              << " control="
              << sar::platform::windows_asio_control_open_error_name(
                     opened.control_open_error)
              << '\n';
    return 9;
  }

  auto before = opened.runtime->summary();
  std::cout << "driver name=\"" << device->label << "\" clsid=\"" << clsid
            << "\"\n";
  std::cout << "negotiated sample_rate=" << before.sample_rate
            << " block_frames=" << before.frames_per_block
            << " inputs=" << before.input_channels
            << " outputs=" << before.output_channels
            << " duration_ms=" << parsed.options.duration_ms << '\n';

  if (opened.runtime->start() !=
      sar::service::WindowsPhysicalAsioRuntimeError::None) {
    const auto failed = opened.runtime->summary();
    print_summary(failed, opened.runtime->drain_host_events());
    return 10;
  }
  std::this_thread::sleep_for(
      std::chrono::milliseconds(parsed.options.duration_ms));
  const auto stop_error = opened.runtime->stop();
  const auto summary = opened.runtime->summary();
  const auto events = opened.runtime->drain_host_events();
  print_summary(summary, events);

  const bool healthy =
      stop_error == sar::service::WindowsPhysicalAsioRuntimeError::None &&
      summary.last_error ==
          sar::service::WindowsPhysicalAsioRuntimeError::None &&
      summary.diagnostics_available && summary.diagnostics.processed_blocks > 0 &&
      summary.diagnostics.xrun_count == 0 && summary.rejected_callbacks == 0 &&
      events.reset_requests == 0 && events.buffer_size_changes == 0 &&
      events.resync_requests == 0;
  std::cout << "healthy=" << (healthy ? 1 : 0) << '\n';
  return healthy ? 0 : 11;
}
