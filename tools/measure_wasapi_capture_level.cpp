#include "core/platform/windows_wasapi_stream.h"
#include "core/platform/windows_wasapi_stream_probe.h"
#include "core/realtime/audio_buffer.h"
#include "tools/capture_level_evidence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

constexpr std::uint32_t kDefaultDurationMs = 3000;
constexpr std::uint32_t kDefaultTimeoutMs = 1000;
constexpr float kNonSilentPeakThreshold = sar::tools::CaptureLevelEvidence::kSignalThreshold;

enum class ExitCode : int {
  Success = 0,
  RuntimeError = 1,
  InvalidArguments = 2,
  Silent = 3,
};

struct Options {
  std::string capture_device_id;
  std::uint32_t duration_ms = kDefaultDurationMs;
  std::uint32_t timeout_ms = kDefaultTimeoutMs;
  bool show_help = false;
};

struct LevelEvidence {
  sar::tools::CaptureLevelEvidence level;
  std::uint64_t silent = 0;
  std::uint64_t timeout = 0;
  std::uint64_t error = 0;
  std::uint64_t silent_signal_frames = 0;
  std::uint64_t elapsed_ms = 0;
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
};

bool parse_u32(std::string_view text, std::uint32_t& value) noexcept {
  if (text.empty()) {
    return false;
  }

  std::uint64_t parsed = 0;
  for (const char character : text) {
    if (character < '0' || character > '9') {
      return false;
    }
    parsed = parsed * 10U + static_cast<std::uint64_t>(character - '0');
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
      return false;
    }
  }
  value = static_cast<std::uint32_t>(parsed);
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--help" || argument == "-h") {
      options.show_help = true;
      return true;
    }
    if (index + 1 >= argc) {
      std::cerr << "Missing value for " << argument << '\n';
      return false;
    }

    const std::string_view value(argv[++index]);
    if (argument == "--capture-id") {
      if (value.empty()) {
        std::cerr << "Empty device ID for --capture-id\n";
        return false;
      }
      options.capture_device_id.assign(value);
    } else if (argument == "--duration-ms") {
      if (!parse_u32(value, options.duration_ms) || options.duration_ms == 0) {
        std::cerr << "Invalid non-zero integer for --duration-ms: " << value << '\n';
        return false;
      }
    } else if (argument == "--timeout-ms") {
      if (!parse_u32(value, options.timeout_ms) || options.timeout_ms == 0) {
        std::cerr << "Invalid non-zero integer for --timeout-ms: " << value << '\n';
        return false;
      }
    } else {
      std::cerr << "Unknown argument: " << argument << '\n';
      return false;
    }
  }

  if (options.capture_device_id.empty()) {
    std::cerr << "--capture-id ID is required\n";
    return false;
  }
  return true;
}

void count_errors(const sar::platform::WasapiStreamResult& result,
                  LevelEvidence& evidence) noexcept {
  evidence.error += result.errors().size();
}

void count_errors(const sar::platform::WasapiStreamIoResult& result,
                  LevelEvidence& evidence) noexcept {
  evidence.error += result.errors().size();
}

std::string frame_index(std::uint64_t value) {
  return value == sar::tools::CaptureLevelEvidence::kNoFrame ? "-1" : std::to_string(value);
}

void print_evidence(const LevelEvidence& evidence) {
  const auto& level = evidence.level;
  std::cout << std::setprecision(9)
            << "wasapi_capture_level"
            << " frames=" << level.frames
            << " peak=" << level.peak
            << " rms=" << level.rms()
            << " nonfinite=" << level.nonfinite
            << " silent=" << evidence.silent
            << " timeout=" << evidence.timeout
            << " error=" << evidence.error
            << " sample_rate=" << evidence.sample_rate << " channels=" << evidence.channels
            << " elapsed_ms=" << evidence.elapsed_ms << " packets=" << level.packets
            << " nonzero_samples=" << level.nonzero_samples
            << " nonzero_frames=" << level.nonzero_frames
            << " signal_frames=" << level.signal_frames
            << " signal_packets=" << level.signal_packets
            << " first_signal_frame=" << frame_index(level.first_signal_frame)
            << " last_signal_frame=" << frame_index(level.last_signal_frame)
            << " first_signal_packet=" << frame_index(level.first_signal_packet)
            << " peak_frame=" << frame_index(level.peak_frame)
            << " maximum_step=" << level.maximum_step
            << " maximum_step_frame=" << frame_index(level.maximum_step_frame)
            << " maximum_step_channel=" << level.maximum_step_channel
            << " silent_signal_frames=" << evidence.silent_signal_frames << '\n';
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    return static_cast<int>(ExitCode::InvalidArguments);
  }
  if (options.show_help) {
    std::cout << "Usage: sar_measure_wasapi_capture_level --capture-id ID "
                 "[--duration-ms N] [--timeout-ms N]\n"
                 "Exit 0: peak >= 1e-5; exit 1: WASAPI error; "
                 "exit 2: invalid arguments; exit 3: silent or no frames.\n";
    return static_cast<int>(ExitCode::Success);
  }

  LevelEvidence evidence;
  auto probe_result = sar::platform::probe_wasapi_stream(
      options.capture_device_id,
      sar::platform::WasapiStreamDirection::Capture);
  if (!probe_result.ok()) {
    evidence.error += probe_result.errors().size();
    print_evidence(evidence);
    return static_cast<int>(ExitCode::RuntimeError);
  }

  auto open_result =
      sar::platform::open_wasapi_stream_shell(probe_result.probe());
  if (!open_result.ok()) {
    evidence.error += open_result.errors().size();
    print_evidence(evidence);
    return static_cast<int>(ExitCode::RuntimeError);
  }

  auto stream = open_result.take_stream();
  const auto& probe = stream.probe();
  evidence.sample_rate = probe.mix_format.sample_rate;
  evidence.channels = probe.mix_format.channels;
  evidence.level = sar::tools::CaptureLevelEvidence(evidence.channels);
  sar::realtime::AudioBuffer capture_buffer(probe.mix_format.channels,
                                            probe.buffer_frames);

  const auto start_result = stream.start();
  if (!start_result.ok()) {
    count_errors(start_result, evidence);
    print_evidence(evidence);
    return static_cast<int>(ExitCode::RuntimeError);
  }

  const auto started = std::chrono::steady_clock::now();
  const auto deadline = started +
                        std::chrono::milliseconds(options.duration_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    const auto capture_result =
        stream.capture_once(capture_buffer, options.timeout_ms);
    if (!capture_result.ok()) {
      count_errors(capture_result, evidence);
      break;
    }
    if (capture_result.timed_out()) {
      ++evidence.timeout;
      continue;
    }
    if (capture_result.cancelled()) {
      ++evidence.error;
      break;
    }

    const auto frames = capture_result.frames();
    const auto previous_signal_frames = evidence.level.signal_frames;
    if (!evidence.level.observe(capture_buffer, frames)) {
      ++evidence.error;
      break;
    }
    if (capture_result.silent()) {
      evidence.silent += frames;
      evidence.silent_signal_frames += evidence.level.signal_frames - previous_signal_frames;
    }
  }

  const auto stop_result = stream.stop();
  if (!stop_result.ok()) {
    count_errors(stop_result, evidence);
  }
  evidence.elapsed_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<
      std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count());
  print_evidence(evidence);

  if (evidence.error != 0 || evidence.level.nonfinite != 0) {
    return static_cast<int>(ExitCode::RuntimeError);
  }
  if (evidence.level.frames == 0 || evidence.level.peak < kNonSilentPeakThreshold) {
    return static_cast<int>(ExitCode::Silent);
  }
  return static_cast<int>(ExitCode::Success);
}
