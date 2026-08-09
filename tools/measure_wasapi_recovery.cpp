#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/node.h"
#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_duplex_supervisor.h"
#include "core/platform/windows_wasapi_endpoint_notification.h"
#include "core/platform/windows_wasapi_render_loop.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "tools/wasapi_measure_report.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <windows.h>

namespace {

struct RecoveryMeasureOptions {
  std::uint32_t duration_ms = 10000;
  std::uint32_t poll_ms = 100;
  std::uint32_t timeout_ms = 10;
  std::string capture_device_id;
  std::string render_device_id;
  bool render_only = false;
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

    auto parse_device_id = [&](std::string& target) {
      if (index + 1 >= argc || argv[index + 1][0] == '\0') {
        std::cerr << "Missing value for " << arg << '\n';
        return false;
      }
      target = argv[++index];
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
    } else if (arg == "--capture-id") {
      if (!parse_device_id(options.capture_device_id)) {
        return false;
      }
    } else if (arg == "--render-id") {
      if (!parse_device_id(options.render_device_id)) {
        return false;
      }
    } else if (arg == "--render-only") {
      options.render_only = true;
    } else {
      std::cerr << "Unknown argument: " << arg << '\n';
      return false;
    }
  }
  if (options.render_only && !options.capture_device_id.empty()) {
    std::cerr << "--render-only cannot be combined with --capture-id\n";
    return false;
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
            << " notification_reopen_pending="
            << (summary.endpoint_notification_reopen_pending ? 1 : 0)
            << " notification_reopen_at_ms="
            << summary.endpoint_notification_reopen_at_ms
            << " last_recovery_duration_ms="
            << summary.last_recovery_duration_ms
            << " maximum_recovery_duration_ms="
            << summary.maximum_recovery_duration_ms
            << " maximum_render_recovery_silence_frames="
            << summary.maximum_render_recovery_silence_frames
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

std::vector<sar::platform::WasapiRealtimeWorkerError>
convert_selection_errors(
    const std::vector<sar::platform::WasapiEndpointSelectionError>& errors) {
  std::vector<sar::platform::WasapiRealtimeWorkerError> converted;
  converted.reserve(errors.size());
  for (const auto& error : errors) {
    converted.push_back({error.code, error.message});
  }
  return converted;
}

int measure_render_recovery(
    const RecoveryMeasureOptions& options,
    sar::platform::WindowsWasapiEndpointNotification& notifications) {
  constexpr char kUnusedCaptureEndpointId[] = "__sar_render_only__";
  const auto render_selection = options.render_device_id.empty()
                                    ? sar::platform::WasapiEndpointSelection::follow_default()
                                    : sar::platform::WasapiEndpointSelection::pinned_device_id(
                                          options.render_device_id);
  const sar::platform::WasapiEndpointSelectionPolicy endpoint_policy(
      sar::platform::WasapiEndpointSelection::pinned_device_id(
          kUnusedCaptureEndpointId),
      render_selection);
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto selected = provider.resolve_endpoint(
      endpoint_policy, sar::platform::WasapiEndpointDirection::Render);
  if (!selected.ok()) {
    for (const auto& error : selected.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }
  const auto probe_result = sar::platform::probe_wasapi_stream(
      selected.device_id(), sar::platform::WasapiStreamDirection::Render);
  if (!probe_result.ok()) {
    print_probe_errors("Selected render stream", probe_result);
    return 1;
  }

  const auto& probe = probe_result.probe();
  sar::tools::print_wasapi_probe(std::cout, "Selected render stream", probe);
  std::cout << "Recovery measurement\n"
            << "  Mode: render-only\n"
            << "  Duration ms: " << options.duration_ms << '\n'
            << "  Poll interval ms: " << options.poll_ms << '\n'
            << "  Timeout ms: " << options.timeout_ms << '\n';
  sar::graph::Graph graph(1, probe.mix_format.channels, probe.buffer_frames,
                          probe.mix_format.sample_rate);
  graph.add_node(std::make_unique<sar::graph::GainNode>(0.0F));
  sar::diagnostics::EngineDiagnostics diagnostics;
  sar::platform::WasapiDuplexRuntimeFactory factory =
      [&graph, &diagnostics, endpoint_policy] {
        sar::platform::WindowsWasapiDeviceProvider reopen_provider;
        const auto reopened_endpoint = reopen_provider.resolve_endpoint(
            endpoint_policy, sar::platform::WasapiEndpointDirection::Render);
        if (!reopened_endpoint.ok()) {
          return sar::platform::WasapiDuplexRuntimeOpenResult::failure(
              convert_selection_errors(reopened_endpoint.errors()));
        }
        auto reopened = sar::platform::open_wasapi_render_loop(
            reopened_endpoint.device_id(), graph, diagnostics);
        if (!reopened.ok()) {
          return sar::platform::WasapiDuplexRuntimeOpenResult::failure(
              reopened.errors());
        }
        sar::platform::WasapiDuplexRuntimeEndpoints endpoints{
            .render_device_id = reopened.loop().probe().device_id};
        return sar::platform::WasapiDuplexRuntimeOpenResult::success(
            reopened.take_loop(), std::move(endpoints));
      };
  sar::platform::WindowsWasapiDuplexSupervisor supervisor(
      std::move(factory), options.timeout_ms, endpoint_policy);

  const auto start = std::chrono::steady_clock::now();
  auto next_poll = start;
  sar::platform::WasapiDuplexSupervisorSummary measurement_summary;
  static_cast<void>(supervisor.poll_endpoint_notifications(notifications, 0));
  supervisor.start(0);
  while (true) {
    const auto elapsed_ms = elapsed_milliseconds(start);
    static_cast<void>(supervisor.poll_endpoint_notifications(notifications,
                                                             elapsed_ms));
    supervisor.tick(elapsed_ms);
    measurement_summary = supervisor.summary();
    print_supervisor_summary(elapsed_ms, measurement_summary);
    if (elapsed_ms >= options.duration_ms) {
      break;
    }
    next_poll += std::chrono::milliseconds(options.poll_ms);
    const auto finish =
        start + std::chrono::milliseconds(options.duration_ms);
    std::this_thread::sleep_until(std::min(next_poll, finish));
  }

  const auto stop_ms = elapsed_milliseconds(start);
  supervisor.stop(stop_ms);
  const auto final_summary = supervisor.summary();
  print_supervisor_summary(stop_ms, final_summary);
  print_last_errors(supervisor.last_errors());
  return !measurement_summary.running ||
                 measurement_summary.active_render_device_id.empty() ||
                 final_summary.failed_recovery_count != 0 ||
                 final_summary.endpoint_notification_reset_failure_count != 0 ||
                 !supervisor.last_errors().empty()
             ? 1
             : 0;
}

}  // namespace

int main(int argc, char** argv) {
  RecoveryMeasureOptions options;
  if (!parse_options(argc, argv, options)) {
    return 2;
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_recovery "
                 "[--duration-ms N] [--poll-ms N] [--timeout-ms N] "
                 "[--capture-id ID] [--render-id ID] [--render-only]\n";
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
    } else if (options.render_only) {
      exit_code = measure_render_recovery(options, notifications);
    } else {
      const auto capture_selection = options.capture_device_id.empty()
                                         ? sar::platform::WasapiEndpointSelection::follow_default()
                                         : sar::platform::WasapiEndpointSelection::pinned_device_id(
                                               options.capture_device_id);
      const auto render_selection = options.render_device_id.empty()
                                        ? sar::platform::WasapiEndpointSelection::follow_default()
                                        : sar::platform::WasapiEndpointSelection::pinned_device_id(
                                              options.render_device_id);
      const sar::platform::WasapiEndpointSelectionPolicy endpoint_policy(
          capture_selection, render_selection);
      sar::platform::WindowsWasapiDeviceProvider provider;
      const auto selected = provider.resolve_endpoint_pair(endpoint_policy);
      if (!selected.ok()) {
        for (const auto& error : selected.errors()) {
          std::cerr << error.code << ": " << error.message << '\n';
        }
        exit_code = 1;
      } else {
        const auto capture_probe_result = sar::platform::probe_wasapi_stream(
          selected.endpoints().capture_device_id,
          sar::platform::WasapiStreamDirection::Capture);
        const auto render_probe_result = sar::platform::probe_wasapi_stream(
          selected.endpoints().render_device_id,
          sar::platform::WasapiStreamDirection::Render);
      if (!capture_probe_result.ok() || !render_probe_result.ok()) {
        if (!capture_probe_result.ok()) {
          print_probe_errors("Selected capture stream", capture_probe_result);
        }
        if (!render_probe_result.ok()) {
          print_probe_errors("Selected render stream", render_probe_result);
        }
        exit_code = 1;
      } else {
        const auto& capture_probe = capture_probe_result.probe();
        const auto& render_probe = render_probe_result.probe();
        sar::tools::print_wasapi_probe(
            std::cout, "Selected capture stream", capture_probe);
        sar::tools::print_wasapi_probe(
            std::cout, "Selected render stream", render_probe);
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
            graph, diagnostics, options.timeout_ms, endpoint_policy);

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
      }

    }
    if (SUCCEEDED(register_result)) {
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
