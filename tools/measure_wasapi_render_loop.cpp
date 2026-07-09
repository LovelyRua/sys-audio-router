#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/platform/windows_wasapi_runtime_summary.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

namespace {

struct Options {
  std::uint32_t duration_ms = 250;
  std::uint32_t timeout_ms = 10;
  bool show_help = false;
};

bool parse_u32(std::string_view text, std::uint32_t& value) {
  if (text.empty()) {
    return false;
  }

  std::uint64_t parsed = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9') {
      return false;
    }
    parsed = parsed * 10 + static_cast<std::uint64_t>(ch - '0');
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
  }

  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      options.show_help = true;
      return true;
    }

    auto parse_next = [&](std::uint32_t& target) {
      if (index + 1 >= argc) {
        std::cerr << "Missing value for " << arg << '\n';
        return false;
      }
      ++index;
      if (!parse_u32(argv[index], target)) {
        std::cerr << "Invalid integer value for " << arg << ": " << argv[index] << '\n';
        return false;
      }
      return true;
    };

    if (arg == "--duration-ms") {
      if (!parse_next(options.duration_ms)) {
        return false;
      }
    } else if (arg == "--timeout-ms") {
      if (!parse_next(options.timeout_ms)) {
        return false;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }

  return true;
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
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_render_loop "
                 "[--duration-ms N] [--timeout-ms N]\n";
    return 0;
  }

  const auto probe_result =
      sar::platform::probe_default_wasapi_stream(sar::platform::WasapiStreamDirection::Render);
  if (!probe_result.ok()) {
    std::cerr << "Default render stream unavailable\n";
    for (const auto& error : probe_result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
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
  return errors.empty() ? 0 : 1;
}
