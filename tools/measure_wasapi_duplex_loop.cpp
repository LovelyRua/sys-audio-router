#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_options.h"
#include "tools/wasapi_measure_report.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

void print_probe_errors(const char* label,
                        const sar::platform::WasapiStreamProbeResult& result) {
  std::cerr << label << " unavailable\n";
  for (const auto& error : result.errors()) {
    std::cerr << error.code << ": " << error.message << '\n';
  }
}

}  // namespace

int main(int argc, char** argv) {
  sar::tools::WasapiMeasureOptions options;
  if (!sar::tools::parse_wasapi_measure_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_duplex_loop "
                 "[--duration-ms N] [--timeout-ms N] [--require-healthy]\n";
    return 0;
  }

  const auto capture_probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Capture);
  const auto render_probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Render);
  if (!capture_probe_result.ok() || !render_probe_result.ok()) {
    if (!capture_probe_result.ok()) {
      print_probe_errors("Default capture stream", capture_probe_result);
    }
    if (!render_probe_result.ok()) {
      print_probe_errors("Default render stream", render_probe_result);
    }
    return 1;
  }

  const auto& capture_probe = capture_probe_result.probe();
  const auto& render_probe = render_probe_result.probe();
  sar::tools::print_wasapi_probe(std::cout, "Default capture stream", capture_probe);
  sar::tools::print_wasapi_probe(std::cout, "Default render stream", render_probe);
  std::cout << "Measurement\n";
  std::cout << "  Duration ms: " << options.duration_ms << '\n';
  std::cout << "  Timeout ms: " << options.timeout_ms << '\n';

  if (capture_probe.mix_format.sample_rate != render_probe.mix_format.sample_rate) {
    std::cerr << "duplex_sample_rate_mismatch: Default WASAPI capture and render "
                 "streams need a sample-rate adapter before duplex use.\n";
    return 1;
  }

  const auto channels =
      std::max(capture_probe.mix_format.channels, render_probe.mix_format.channels);
  const auto frames = std::max(capture_probe.buffer_frames, render_probe.buffer_frames);
  sar::graph::Graph graph(1, channels, frames, capture_probe.mix_format.sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;

  auto open_result = sar::platform::open_default_wasapi_duplex_loop(graph, diagnostics);
  if (!open_result.ok()) {
    std::cerr << "Duplex loop open failed\n";
    for (const auto& error : open_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = open_result.take_loop();
  const auto start_result = loop->start(options.timeout_ms);
  if (!start_result.ok()) {
    std::cerr << "Duplex loop start failed\n";
    for (const auto& error : start_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(options.duration_ms));
  loop->stop();

  const auto errors = loop->last_errors();
  if (!errors.empty()) {
    std::cerr << "Duplex loop worker errors\n";
    for (const auto& error : errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
  }

  const auto loop_summary = loop->summary();
  sar::tools::print_wasapi_runtime_summary(std::cout, loop_summary.runtime);
  sar::tools::print_wasapi_stream_diagnostics(std::cout,
                                              "Capture stream diagnostics",
                                              loop_summary.capture_stream);
  sar::tools::print_wasapi_stream_diagnostics(std::cout,
                                              "Render stream diagnostics",
                                              loop_summary.render_stream);
  sar::tools::print_wasapi_worker_stats(std::cout, loop_summary.worker);
  sar::tools::print_wasapi_engine_diagnostics(std::cout, diagnostics);
  if (sar::platform::wasapi_runtime_summary_should_fail(loop_summary.runtime,
                                                        options.require_healthy)) {
    return 1;
  }
  return errors.empty() ? 0 : 1;
}
