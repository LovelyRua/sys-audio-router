#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_duplex_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_options.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

namespace {

void print_probe(const char* label, const sar::platform::WasapiStreamProbe& probe) {
  std::cout << label << '\n';
  std::cout << "  Device: " << probe.device_label << '\n';
  std::cout << "  Sample rate: " << probe.mix_format.sample_rate << '\n';
  std::cout << "  Channels: " << probe.mix_format.channels << '\n';
  std::cout << "  Buffer frames: " << probe.buffer_frames << '\n';
}

void print_probe_errors(const char* label,
                        const sar::platform::WasapiStreamProbeResult& result) {
  std::cerr << label << " unavailable\n";
  for (const auto& error : result.errors()) {
    std::cerr << error.code << ": " << error.message << '\n';
  }
}

void print_runtime_summary(const sar::platform::WasapiRuntimeSummary& summary) {
  std::cout << "Runtime summary\n";
  std::cout << "  Health: "
            << sar::platform::wasapi_runtime_health_name(summary.health) << '\n';
  std::cout << "  Reason code: " << summary.reason_code << '\n';
  std::cout << "  Reason: " << summary.reason << '\n';
  std::cout << "  Error count: " << summary.error_count << '\n';
  if (!summary.first_error_code.empty()) {
    std::cout << "  First error code: " << summary.first_error_code << '\n';
    std::cout << "  First error message: " << summary.first_error_message << '\n';
  }
  std::cout << "  Capture buffer frames: " << summary.capture_buffer_frames << '\n';
  std::cout << "  Render buffer frames: " << summary.render_buffer_frames << '\n';
}

void print_worker_stats(const sar::platform::WasapiRealtimeWorkerStats& stats) {
  std::cout << "Worker stats\n";
  std::cout << "  Loop cycles: " << stats.loop_cycles << '\n';
  std::cout << "  Graph processed cycles: " << stats.graph_processed_cycles << '\n';
  std::cout << "  Idle cycles: " << stats.idle_cycles << '\n';
  std::cout << "  Capture idle cycles: " << stats.capture_idle_cycles << '\n';
  std::cout << "  Render idle cycles: " << stats.render_idle_cycles << '\n';
  std::cout << "  Wait timeout cycles: " << stats.wait_timeout_cycles << '\n';
  std::cout << "  Capture wait timeout cycles: "
            << stats.capture_wait_timeout_cycles << '\n';
  std::cout << "  Render wait timeout cycles: "
            << stats.render_wait_timeout_cycles << '\n';
  std::cout << "  Capture partial cycles: " << stats.capture_partial_cycles << '\n';
  std::cout << "  Capture partial frames: " << stats.capture_partial_frames << '\n';
  std::cout << "  Render partial cycles: " << stats.render_partial_cycles << '\n';
  std::cout << "  Render partial frames: " << stats.render_partial_frames << '\n';
  std::cout << "  Capture silent cycles: " << stats.capture_silent_cycles << '\n';
  std::cout << "  Capture silent frames: " << stats.capture_silent_frames << '\n';
  std::cout << "  Captured frames: " << stats.captured_frames << '\n';
  std::cout << "  Rendered frames: " << stats.rendered_frames << '\n';
  std::cout << "  Last captured frames: " << stats.last_captured_frames << '\n';
  std::cout << "  Last rendered frames: " << stats.last_rendered_frames << '\n';
  std::cout << "  Stream start error cycles: " << stats.stream_start_error_cycles << '\n';
  std::cout << "  Stream stop error cycles: " << stats.stream_stop_error_cycles << '\n';
  std::cout << "  Process error cycles: " << stats.process_error_cycles << '\n';
  std::cout << "  Last stop wait us: " << stats.last_stop_wait_microseconds << '\n';
}

void print_engine_diagnostics(const sar::diagnostics::EngineDiagnostics& diagnostics) {
  std::cout << "Engine diagnostics\n";
  std::cout << "  Graph version: " << diagnostics.graph_version << '\n';
  std::cout << "  Processed blocks: " << diagnostics.processed_blocks << '\n';
  std::cout << "  Xruns: " << diagnostics.xrun_count << '\n';
  std::cout << "  Last callback seconds: " << diagnostics.last_callback_seconds << '\n';
  std::cout << "  Peak callback seconds: " << diagnostics.peak_callback_seconds << '\n';
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
  print_probe("Default capture stream", capture_probe);
  print_probe("Default render stream", render_probe);
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
  print_runtime_summary(loop_summary.runtime);
  print_worker_stats(loop_summary.worker);
  print_engine_diagnostics(diagnostics);
  if (options.require_healthy &&
      loop_summary.runtime.health != sar::platform::WasapiRuntimeHealth::Healthy) {
    return 1;
  }
  return errors.empty() ? 0 : 1;
}
