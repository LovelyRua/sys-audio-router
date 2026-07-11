#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_loopback_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_options.h"
#include "tools/wasapi_measure_report.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

int main(int argc, char** argv) {
  sar::tools::WasapiMeasureOptions options;
  if (!sar::tools::parse_wasapi_measure_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_loopback_loop "
                 "[--duration-ms N] [--timeout-ms N] [--require-healthy]\n";
    return 0;
  }

  const auto probe_result = sar::platform::probe_default_wasapi_stream(
      sar::platform::WasapiStreamDirection::Capture,
      sar::platform::WasapiStreamMode::Loopback);
  if (!probe_result.ok()) {
    std::cerr << "Default loopback stream unavailable\n";
    for (const auto& error : probe_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  const auto& probe = probe_result.probe();
  sar::tools::print_wasapi_probe(
      std::cout, "Default loopback stream", probe);
  std::cout << "Measurement\n"
            << "  Duration ms: " << options.duration_ms << '\n'
            << "  Timeout ms: " << options.timeout_ms << '\n';

  sar::graph::Graph graph(1,
                          probe.mix_format.channels,
                          probe.buffer_frames,
                          probe.mix_format.sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  auto open_result =
      sar::platform::open_default_wasapi_loopback_loop(graph, diagnostics);
  if (!open_result.ok()) {
    std::cerr << "Loopback loop open failed\n";
    for (const auto& error : open_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = open_result.take_loop();
  const auto start_result = loop->start(options.timeout_ms);
  if (!start_result.ok()) {
    std::cerr << "Loopback loop start failed\n";
    for (const auto& error : start_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::cout << "wasapi_lifecycle phase=started mode=loopback\n" << std::flush;
  std::this_thread::sleep_for(std::chrono::milliseconds(options.duration_ms));
  std::cout << "wasapi_lifecycle phase=stopping mode=loopback\n" << std::flush;
  loop->stop();
  std::cout << "wasapi_lifecycle phase=stopped mode=loopback\n" << std::flush;

  const auto errors = loop->last_errors();
  if (!errors.empty()) {
    std::cerr << "Loopback loop worker errors\n";
    for (const auto& error : errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
  }

  const auto summary = loop->summary();
  sar::tools::print_wasapi_runtime_summary(std::cout, summary.runtime);
  sar::tools::print_wasapi_stream_diagnostics(
      std::cout, "Loopback stream diagnostics", summary.capture_stream);
  sar::tools::print_wasapi_worker_stats(std::cout, summary.worker);
  sar::tools::print_wasapi_engine_diagnostics(std::cout, diagnostics);

  if (sar::platform::wasapi_runtime_summary_should_fail(
          summary.runtime, options.require_healthy)) {
    return 1;
  }
  return errors.empty() ? 0 : 1;
}
