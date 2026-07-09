#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_options.h"

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

void print_worker_stats(const sar::platform::WasapiRealtimeWorkerStats& stats) {
  std::cout << "Worker stats\n";
  std::cout << "  Loop cycles: " << stats.loop_cycles << '\n';
  std::cout << "  Graph processed cycles: " << stats.graph_processed_cycles << '\n';
  std::cout << "  Idle cycles: " << stats.idle_cycles << '\n';
  std::cout << "  Render idle cycles: " << stats.render_idle_cycles << '\n';
  std::cout << "  Wait timeout cycles: " << stats.wait_timeout_cycles << '\n';
  std::cout << "  Render wait timeout cycles: " << stats.render_wait_timeout_cycles << '\n';
  std::cout << "  Render partial cycles: " << stats.render_partial_cycles << '\n';
  std::cout << "  Render partial frames: " << stats.render_partial_frames << '\n';
  std::cout << "  Rendered frames: " << stats.rendered_frames << '\n';
  std::cout << "  Last rendered frames: " << stats.last_rendered_frames << '\n';
  std::cout << "  Stream start error cycles: " << stats.stream_start_error_cycles << '\n';
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
  std::cout << "  Render buffer frames: " << summary.render_buffer_frames << '\n';
  std::cout << "  Rendered frames: " << summary.rendered_frames << '\n';
  std::cout << "  Last rendered frames: " << summary.last_rendered_frames << '\n';
  std::cout << "  Last stop wait us: " << summary.last_stop_wait_microseconds << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  sar::tools::WasapiMeasureOptions options;
  if (!sar::tools::parse_wasapi_measure_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_render_loop "
                 "[--duration-ms N] [--timeout-ms N] [--require-healthy]\n";
    return 0;
  }

  const auto probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Render);
  if (!probe_result.ok()) {
    print_probe_errors("Default render stream", probe_result);
    return 1;
  }

  const auto& probe = probe_result.probe();
  std::cout << "Default render stream\n";
  std::cout << "  Device: " << probe.device_label << '\n';
  std::cout << "  Sample rate: " << probe.mix_format.sample_rate << '\n';
  std::cout << "  Channels: " << probe.mix_format.channels << '\n';
  std::cout << "  Buffer frames: " << probe.buffer_frames << '\n';
  std::cout << "  Duration ms: " << options.duration_ms << '\n';
  std::cout << "  Timeout ms: " << options.timeout_ms << '\n';

  sar::graph::Graph graph(1,
                          probe.mix_format.channels,
                          probe.buffer_frames,
                          probe.mix_format.sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;

  auto open_result = sar::platform::open_default_wasapi_render_loop(graph, diagnostics);
  if (!open_result.ok()) {
    std::cerr << "Render loop open failed\n";
    for (const auto& error : open_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  auto loop = open_result.take_loop();
  auto& input = loop->input_buffer();
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = 0.0F;
    }
  }

  const auto start_result = loop->start(options.timeout_ms);
  if (!start_result.ok()) {
    std::cerr << "Render loop start failed\n";
    for (const auto& error : start_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(options.duration_ms));
  loop->stop();

  const auto errors = loop->last_errors();
  if (!errors.empty()) {
    std::cerr << "Render loop worker errors\n";
    for (const auto& error : errors) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
  }

  const auto loop_summary = loop->summary();
  const auto stats = loop_summary.worker;
  print_runtime_summary(loop_summary.runtime);
  print_worker_stats(stats);
  print_engine_diagnostics(diagnostics);
  if (sar::platform::wasapi_runtime_summary_should_fail(loop_summary.runtime,
                                                        options.require_healthy)) {
    return 1;
  }
  return errors.empty() ? 0 : 1;
}
