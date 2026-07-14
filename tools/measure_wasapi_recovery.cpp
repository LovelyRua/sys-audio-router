#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_duplex_supervisor.h"
#include "core/platform/windows_wasapi_endpoint_notification.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_report.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>

namespace {

struct RecoveryMeasureOptions {
  std::uint32_t duration_ms = 10000;
  std::uint32_t poll_ms = 100;
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

bool parse_options(int argc, char** argv, RecoveryMeasureOptions& options) {
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
        std::cerr << "Invalid integer value for " << arg << ": "
                  << argv[index] << '\n';
        return false;
      }
      return true;
    };

    if (arg == "--duration-ms") {
      if (!parse_next(options.duration_ms) || options.duration_ms == 0) {
        if (options.duration_ms == 0) {
          std::cerr << "Value for --duration-ms must be greater than zero\n";
        }
        return false;
      }
    } else if (arg == "--poll-ms") {
      if (!parse_next(options.poll_ms) || options.poll_ms == 0) {
        if (options.poll_ms == 0) {
          std::cerr << "Value for --poll-ms must be greater than zero\n";
        }
        return false;
      }
    } else if (arg == "--timeout-ms") {
      if (!parse_next(options.timeout_ms) || options.timeout_ms == 0) {
        if (options.timeout_ms == 0) {
          std::cerr << "Value for --timeout-ms must be greater than zero\n";
        }
        return false;
      }
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }
  return true;
}

const char* recovery_state_name(
    sar::platform::WasapiRecoveryState state) noexcept {
  using sar::platform::WasapiRecoveryState;
  switch (state) {
    case WasapiRecoveryState::Stopped:
      return "stopped";
    case WasapiRecoveryState::Opening:
      return "opening";
    case WasapiRecoveryState::Running:
      return "running";
    case WasapiRecoveryState::Quiescing:
      return "quiescing";
    case WasapiRecoveryState::Backoff:
      return "backoff";
    case WasapiRecoveryState::Faulted:
      return "faulted";
  }
  return "unknown";
}

void print_probe_errors(
    const char* label,
    const sar::platform::WasapiStreamProbeResult& result) {
  std::cerr << label << " unavailable\n";
  for (const auto& error : result.errors()) {
    std::cerr << error.code << ": " << error.message << '\n';
  }
}

void print_supervisor_summary(
    std::uint64_t elapsed_ms,
    const sar::platform::WasapiDuplexSupervisorSummary& summary) {
  std::cout << "wasapi_recovery_supervisor"
            << " elapsed_ms=" << elapsed_ms
            << " state=" << recovery_state_name(summary.state)
            << " running=" << (summary.running ? 1 : 0)
            << " attempt_count=" << summary.attempt_count
            << " runtime_open_count=" << summary.runtime_open_count
            << " recovery_episode_count=" << summary.recovery_episode_count
            << " successful_recovery_count="
            << summary.successful_recovery_count
            << " failed_recovery_count=" << summary.failed_recovery_count
            << " notification_reopen_count="
            << summary.endpoint_notification_reopen_count
            << " notification_reset_failure_count="
            << summary.endpoint_notification_reset_failure_count
            << " last_recovery_duration_ms="
            << summary.last_recovery_duration_ms
            << " maximum_recovery_duration_ms="
            << summary.maximum_recovery_duration_ms
            << " error_count=" << summary.error_count
            << " capture_generation="
            << summary.capture_endpoint_generation
            << " render_generation=" << summary.render_endpoint_generation
            << " active_capture_device_id="
            << std::quoted(summary.active_capture_device_id)
            << " active_render_device_id="
            << std::quoted(summary.active_render_device_id)
            << '\n';
}

void print_last_errors(
    const std::vector<sar::platform::WasapiRealtimeWorkerError>& errors) {
  std::cout << "wasapi_recovery_last_errors count=" << errors.size() << '\n';
  if (errors.empty()) {
    return;
  }
  std::cerr << "WASAPI recovery last errors\n";
  for (const auto& error : errors) {
    std::cerr << error.code << ": " << error.message;
    if (error.native_hresult.has_value()) {
      std::cerr << " (HRESULT=" << *error.native_hresult << ')';
    }
    if (error.native_win32_code.has_value()) {
      std::cerr << " (Win32=" << *error.native_win32_code << ')';
    }
    std::cerr << '\n';
  }
}

std::uint64_t elapsed_milliseconds(
    std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

}  // namespace

int main(int argc, char** argv) {
  RecoveryMeasureOptions options;
  if (!parse_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_recovery "
                 "[--duration-ms N] [--poll-ms N] [--timeout-ms N]\n";
    return 0;
  }

  const auto com_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com_result)) {
    std::cerr << "COM initialization failed: " << com_result << '\n';
    return 1;
  }
  const bool uninitialize_com = true;

  int exit_code = 0;
  {
    sar::platform::WindowsWasapiEndpointNotification notifications;
    const auto register_result = notifications.register_notifications();
    if (FAILED(register_result)) {
      std::cerr << "Endpoint notification registration failed: "
                << register_result << '\n';
      exit_code = 1;
    } else {
      const auto capture_probe_result = sar::platform::probe_default_wasapi_stream(
          sar::platform::WasapiStreamDirection::Capture);
      const auto render_probe_result = sar::platform::probe_default_wasapi_stream(
          sar::platform::WasapiStreamDirection::Render);
      if (!capture_probe_result.ok() || !render_probe_result.ok()) {
        if (!capture_probe_result.ok()) {
          print_probe_errors("Default capture stream", capture_probe_result);
        }
        if (!render_probe_result.ok()) {
          print_probe_errors("Default render stream", render_probe_result);
        }
        exit_code = 1;
      } else {
        const auto& capture_probe = capture_probe_result.probe();
        const auto& render_probe = render_probe_result.probe();
        sar::tools::print_wasapi_probe(
            std::cout, "Default capture stream", capture_probe);
        sar::tools::print_wasapi_probe(
            std::cout, "Default render stream", render_probe);
        std::cout << "Recovery measurement\n"
                  << "  Duration ms: " << options.duration_ms << '\n'
                  << "  Poll interval ms: " << options.poll_ms << '\n'
                  << "  Timeout ms: " << options.timeout_ms << '\n';

        const auto channels = std::max(capture_probe.mix_format.channels,
                                       render_probe.mix_format.channels);
        const auto frames = std::max(capture_probe.buffer_frames,
                                     render_probe.buffer_frames);
        sar::graph::Graph graph(
            1, channels, frames, render_probe.mix_format.sample_rate);
        graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
        sar::diagnostics::EngineDiagnostics diagnostics;
        sar::platform::WindowsWasapiDuplexSupervisor supervisor(
            graph, diagnostics, options.timeout_ms);

        const auto start = std::chrono::steady_clock::now();
        auto next_poll = start;
        sar::platform::WasapiDuplexSupervisorSummary measurement_summary;
        (void)supervisor.poll_endpoint_notifications(notifications, 0);
        supervisor.start(0);
        while (true) {
          const auto elapsed_ms = elapsed_milliseconds(start);
          (void)supervisor.poll_endpoint_notifications(notifications,
                                                       elapsed_ms);
          supervisor.tick(elapsed_ms);
          measurement_summary = supervisor.summary();
          print_supervisor_summary(elapsed_ms, measurement_summary);
          if (elapsed_ms >= options.duration_ms) {
            break;
          }
          next_poll += std::chrono::milliseconds(options.poll_ms);
          const auto finish = start +
                              std::chrono::milliseconds(options.duration_ms);
          std::this_thread::sleep_until(std::min(next_poll, finish));
        }

        const auto stop_ms = elapsed_milliseconds(start);
        supervisor.stop(stop_ms);
        const auto final_summary = supervisor.summary();
        print_supervisor_summary(stop_ms, final_summary);
        print_last_errors(supervisor.last_errors());
        if (!measurement_summary.running ||
            measurement_summary.active_capture_device_id.empty() ||
            measurement_summary.active_render_device_id.empty() ||
            final_summary.failed_recovery_count != 0 ||
            final_summary.endpoint_notification_reset_failure_count != 0 ||
            !supervisor.last_errors().empty()) {
          exit_code = 1;
        }
      }

      const auto unregister_result = notifications.unregister_notifications();
      if (FAILED(unregister_result)) {
        std::cerr << "Endpoint notification unregistration failed: "
                  << unregister_result << '\n';
        exit_code = 1;
      }
    }
  }

  if (uninitialize_com) {
    CoUninitialize();
  }
  return exit_code;
}
