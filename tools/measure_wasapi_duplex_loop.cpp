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

  std::cout << "wasapi_lifecycle phase=started mode=duplex\n" << std::flush;
  std::this_thread::sleep_for(std::chrono::milliseconds(options.duration_ms));
  std::cout << "wasapi_lifecycle phase=stopping mode=duplex\n" << std::flush;
  loop->stop();
  std::cout << "wasapi_lifecycle phase=stopped mode=duplex\n" << std::flush;

  const auto errors = loop->last_errors();
  if (!errors.empty()) {
    std::cerr << "Duplex loop worker errors\n";
    for (const auto& error : errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
  }

  const auto loop_summary = loop->summary();
  std::cout << "wasapi_duplex_clock"
            << " capture_available=" << (loop_summary.capture_clock_available ? 1 : 0)
            << " capture_position=" << loop_summary.capture_clock.position
            << " capture_qpc_100ns=" << loop_summary.capture_clock.qpc_position_100ns
            << " capture_frequency=" << loop_summary.capture_clock.frequency
            << " render_available=" << (loop_summary.render_clock_available ? 1 : 0)
            << " render_position=" << loop_summary.render_clock.position
            << " render_qpc_100ns=" << loop_summary.render_clock.qpc_position_100ns
            << " render_frequency=" << loop_summary.render_clock.frequency
            << " capture_drift_valid=" << (loop_summary.capture_drift.valid ? 1 : 0)
            << " capture_observed_rate="
            << loop_summary.capture_drift.observed_sample_rate
            << " capture_error_ppm=" << loop_summary.capture_drift.nominal_error_ppm
            << " render_drift_valid=" << (loop_summary.render_drift.valid ? 1 : 0)
            << " render_observed_rate=" << loop_summary.render_drift.observed_sample_rate
            << " render_error_ppm=" << loop_summary.render_drift.nominal_error_ppm
            << " frame_balance=" << loop_summary.frame_balance << '\n';
  std::cout << "Duplex clocks\n";
  std::cout << "  Capture available: "
            << (loop_summary.capture_clock_available ? "yes" : "no") << '\n';
  std::cout << "  Capture position: " << loop_summary.capture_clock.position << '\n';
  std::cout << "  Capture QPC 100ns: "
            << loop_summary.capture_clock.qpc_position_100ns << '\n';
  std::cout << "  Capture frequency: " << loop_summary.capture_clock.frequency << '\n';
  std::cout << "  Render available: "
            << (loop_summary.render_clock_available ? "yes" : "no") << '\n';
  std::cout << "  Render position: " << loop_summary.render_clock.position << '\n';
  std::cout << "  Render QPC 100ns: " << loop_summary.render_clock.qpc_position_100ns
            << '\n';
  std::cout << "  Render frequency: " << loop_summary.render_clock.frequency << '\n';
  std::cout << "  Capture drift valid: "
            << (loop_summary.capture_drift.valid ? "yes" : "no") << '\n';
  std::cout << "  Capture observed sample rate: "
            << loop_summary.capture_drift.observed_sample_rate << '\n';
  std::cout << "  Capture nominal error ppm: "
            << loop_summary.capture_drift.nominal_error_ppm << '\n';
  std::cout << "  Render drift valid: "
            << (loop_summary.render_drift.valid ? "yes" : "no") << '\n';
  std::cout << "  Render observed sample rate: "
            << loop_summary.render_drift.observed_sample_rate << '\n';
  std::cout << "  Render nominal error ppm: "
            << loop_summary.render_drift.nominal_error_ppm << '\n';
  std::cout << "  Captured-rendered frame balance: " << loop_summary.frame_balance
            << '\n';
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
