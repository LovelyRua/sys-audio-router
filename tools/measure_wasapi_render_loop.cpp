#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_options.h"
#include "tools/wasapi_measure_report.h"

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
  sar::tools::print_wasapi_probe(std::cout, "Default render stream", probe);
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
  sar::tools::print_wasapi_runtime_summary(std::cout, loop_summary.runtime);
  sar::tools::print_wasapi_stream_diagnostics(std::cout,
                                              "Render stream diagnostics",
                                              loop_summary.render_stream);
  sar::tools::print_wasapi_worker_stats(std::cout, stats);
  sar::tools::print_wasapi_engine_diagnostics(std::cout, diagnostics);
  if (sar::platform::wasapi_runtime_summary_should_fail(loop_summary.runtime,
                                                        options.require_healthy)) {
    return 1;
  }
  return errors.empty() ? 0 : 1;
}
