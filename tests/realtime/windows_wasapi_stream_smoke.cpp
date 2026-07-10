#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_stream.h"

#include <iostream>
#include <string>
#include <utility>

namespace {

bool has_error_code(const sar::platform::WasapiStreamResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

bool has_error_code(const sar::platform::WasapiStreamIoResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::platform::WasapiStreamProbe make_probe() {
  sar::platform::WasapiStreamProbe probe;
  probe.direction = sar::platform::WasapiStreamDirection::Render;
  probe.device_id = "device";
  probe.device_label = "Device";
  probe.mix_format.sample_rate = 48000;
  probe.mix_format.channels = 2;
  probe.mix_format.frames_per_block = 480;
  probe.default_period_100ns = 100000;
  probe.minimum_period_100ns = 30000;
  probe.buffer_frames = 480;
  return probe;
}

bool has_default_output_device() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return false;
  }

  for (const auto& device : result.devices()) {
    if (device.direction == sar::platform::AudioDeviceDirection::Output &&
        device.is_default) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  {
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       sar::platform::WasapiStreamState::Closed)) == "closed",
                   "Expected closed stream state name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       sar::platform::WasapiStreamState::Open)) == "open",
                   "Expected open stream state name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       sar::platform::WasapiStreamState::Started)) == "started",
                   "Expected started stream state name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       sar::platform::WasapiStreamIoStatus::Completed)) ==
                       "completed",
                   "Expected completed I/O status name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       sar::platform::WasapiStreamIoStatus::Idle)) == "idle",
                   "Expected idle I/O status name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       sar::platform::WasapiStreamIoStatus::TimedOut)) == "timed_out",
                   "Expected timed-out I/O status name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       sar::platform::WasapiStreamIoStatus::Cancelled)) == "cancelled",
                   "Expected cancelled I/O status name")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       sar::platform::WasapiStreamIoStatus::Failed)) == "failed",
                   "Expected failed I/O status name")) {
      return failure;
    }
  }

  {
    const auto idle_result = sar::platform::WasapiStreamIoResult::success(0);
    if (const auto failure = expect(idle_result.ok(), "Expected idle I/O success")) {
      return failure;
    }
    if (const auto failure = expect(idle_result.idle(), "Expected zero-frame I/O idle")) {
      return failure;
    }
    if (const auto failure = expect(!idle_result.timed_out(),
                                    "Expected idle I/O not to be timed out")) {
      return failure;
    }
    if (const auto failure = expect(!idle_result.silent(),
                                    "Expected idle I/O not to be silent")) {
      return failure;
    }
    if (const auto failure =
            expect(idle_result.status() == sar::platform::WasapiStreamIoStatus::Idle,
                   "Expected idle I/O status")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       idle_result.status())) == "idle",
                   "Expected idle I/O status name from result")) {
      return failure;
    }
    if (const auto failure = expect(idle_result.frames() == 0,
                                    "Expected idle I/O zero frames")) {
      return failure;
    }
    if (const auto failure = expect(idle_result.errors().empty(),
                                    "Expected idle I/O without errors")) {
      return failure;
    }

    const auto completed_result = sar::platform::WasapiStreamIoResult::success(256);
    if (const auto failure = expect(completed_result.ok(),
                                    "Expected completed I/O success")) {
      return failure;
    }
    if (const auto failure =
            expect(completed_result.status() == sar::platform::WasapiStreamIoStatus::Completed,
                   "Expected completed I/O status")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       completed_result.status())) == "completed",
                   "Expected completed I/O status name from result")) {
      return failure;
    }
    if (const auto failure = expect(completed_result.frames() == 256,
                                    "Expected completed I/O frame count")) {
      return failure;
    }
    if (const auto failure = expect(!completed_result.idle(),
                                    "Expected completed I/O not idle")) {
      return failure;
    }
    if (const auto failure = expect(!completed_result.silent(),
                                    "Expected completed I/O not silent")) {
      return failure;
    }
    if (const auto failure = expect(!completed_result.timed_out(),
                                    "Expected completed I/O not timed out")) {
      return failure;
    }

    const auto timeout_result = sar::platform::WasapiStreamIoResult::timeout();
    if (const auto failure = expect(timeout_result.ok(), "Expected timeout I/O success")) {
      return failure;
    }
    if (const auto failure = expect(timeout_result.timed_out(),
                                    "Expected timeout I/O status")) {
      return failure;
    }
    if (const auto failure = expect(!timeout_result.idle(),
                                    "Expected timeout I/O not to be generic idle")) {
      return failure;
    }
    if (const auto failure =
            expect(timeout_result.status() == sar::platform::WasapiStreamIoStatus::TimedOut,
                   "Expected timeout I/O status enum")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       timeout_result.status())) == "timed_out",
                   "Expected timeout I/O status name from result")) {
      return failure;
    }
    if (const auto failure = expect(timeout_result.frames() == 0,
                                    "Expected timeout I/O zero frames")) {
      return failure;
    }
    if (const auto failure = expect(!timeout_result.silent(),
                                    "Expected timeout I/O not silent")) {
      return failure;
    }
    if (const auto failure = expect(!timeout_result.data_discontinuity(),
                                    "Expected timeout I/O without discontinuity")) {
      return failure;
    }
    if (const auto failure = expect(!timeout_result.timestamp_error(),
                                    "Expected timeout I/O without timestamp error")) {
      return failure;
    }
    if (const auto failure = expect(!timeout_result.cancelled(),
                                    "Expected timeout I/O not cancelled")) {
      return failure;
    }

    const auto cancelled_result = sar::platform::WasapiStreamIoResult::cancellation();
    if (const auto failure = expect(cancelled_result.ok(),
                                    "Expected cancelled I/O success")) {
      return failure;
    }
    if (const auto failure =
            expect(cancelled_result.status() == sar::platform::WasapiStreamIoStatus::Cancelled,
                   "Expected cancelled I/O status enum")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       cancelled_result.status())) == "cancelled",
                   "Expected cancelled I/O status name from result")) {
      return failure;
    }
    if (const auto failure = expect(cancelled_result.frames() == 0,
                                    "Expected cancelled I/O zero frames")) {
      return failure;
    }
    if (const auto failure = expect(cancelled_result.cancelled(),
                                    "Expected cancelled I/O flag")) {
      return failure;
    }
    if (const auto failure = expect(!cancelled_result.timed_out(),
                                    "Expected cancelled I/O not timed out")) {
      return failure;
    }
    if (const auto failure = expect(!cancelled_result.idle(),
                                    "Expected cancelled I/O not idle")) {
      return failure;
    }

    const auto silent_result = sar::platform::WasapiStreamIoResult::success_silent(128);
    if (const auto failure = expect(silent_result.ok(), "Expected silent I/O success")) {
      return failure;
    }
    if (const auto failure = expect(silent_result.silent(), "Expected silent I/O flag")) {
      return failure;
    }
    if (const auto failure = expect(!silent_result.idle(),
                                    "Expected non-zero silent I/O not idle")) {
      return failure;
    }
    if (const auto failure =
            expect(silent_result.status() == sar::platform::WasapiStreamIoStatus::Completed,
                   "Expected non-zero silent I/O completed status")) {
      return failure;
    }
    if (const auto failure = expect(silent_result.frames() == 128,
                                    "Expected silent I/O frame count")) {
      return failure;
    }
    if (const auto failure = expect(!silent_result.timed_out(),
                                    "Expected silent I/O not timed out")) {
      return failure;
    }

    const auto discontinuous_result =
        sar::platform::WasapiStreamIoResult::success(128, true, true);
    if (const auto failure = expect(discontinuous_result.data_discontinuity(),
                                    "Expected capture discontinuity flag")) {
      return failure;
    }
    if (const auto failure = expect(discontinuous_result.timestamp_error(),
                                    "Expected capture timestamp error flag")) {
      return failure;
    }
    if (const auto failure = expect(!discontinuous_result.silent(),
                                    "Expected discontinuous non-silent I/O")) {
      return failure;
    }

    const auto discontinuous_silent_result =
        sar::platform::WasapiStreamIoResult::success_silent(64, true, false);
    if (const auto failure = expect(discontinuous_silent_result.silent() &&
                                        discontinuous_silent_result.data_discontinuity() &&
                                        !discontinuous_silent_result.timestamp_error(),
                                    "Expected independent silent and discontinuity flags")) {
      return failure;
    }

    const auto zero_silent_result = sar::platform::WasapiStreamIoResult::success_silent(0);
    if (const auto failure = expect(zero_silent_result.ok(),
                                    "Expected zero silent I/O success")) {
      return failure;
    }
    if (const auto failure = expect(zero_silent_result.idle(),
                                    "Expected zero silent I/O idle")) {
      return failure;
    }
    if (const auto failure = expect(!zero_silent_result.silent(),
                                    "Expected zero silent I/O not flagged silent")) {
      return failure;
    }
    if (const auto failure =
            expect(zero_silent_result.status() == sar::platform::WasapiStreamIoStatus::Idle,
                   "Expected zero silent I/O idle status")) {
      return failure;
    }

    const auto failure_result = sar::platform::WasapiStreamIoResult::failure({
        {"io_failed", "Synthetic I/O failure."},
    });
    if (const auto failure = expect(!failure_result.ok(), "Expected failed I/O result")) {
      return failure;
    }
    if (const auto failure =
            expect(failure_result.status() == sar::platform::WasapiStreamIoStatus::Failed,
                   "Expected failed I/O status")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_io_status_name(
                       failure_result.status())) == "failed",
                   "Expected failed I/O status name from result")) {
      return failure;
    }
    if (const auto failure = expect(failure_result.frames() == 0,
                                    "Expected failed I/O zero frames")) {
      return failure;
    }
    if (const auto failure = expect(!failure_result.idle(),
                                    "Expected failed I/O not idle")) {
      return failure;
    }
    if (const auto failure = expect(!failure_result.silent(),
                                    "Expected failed I/O not silent")) {
      return failure;
    }
    if (const auto failure = expect(!failure_result.data_discontinuity() &&
                                        !failure_result.timestamp_error(),
                                    "Expected failed I/O without capture flags")) {
      return failure;
    }
    if (const auto failure = expect(!failure_result.timed_out(),
                                    "Expected failed I/O not timed out")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(failure_result, "io_failed"),
                                    "Expected failed I/O error code")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected initial closed stream state")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       stream.state())) == "closed",
                   "Expected initial closed stream state name")) {
      return failure;
    }

    auto result = stream.start();
    if (const auto failure = expect(!result.ok(), "Expected start-before-open failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "stream_not_open"),
                                    "Expected stream_not_open error")) {
      return failure;
    }

    result = stream.open(make_probe());
    if (const auto failure = expect(result.ok(), "Expected stream open success")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Open,
                                    "Expected open stream state")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       stream.state())) == "open",
                   "Expected open stream state name")) {
      return failure;
    }
    auto diagnostics = stream.diagnostics();
    if (const auto failure =
            expect(diagnostics.state == sar::platform::WasapiStreamState::Open,
                   "Expected open diagnostics state")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.mix_format.sample_rate == 48000,
                                    "Expected diagnostics sample rate")) {
      return failure;
    }
    if (const auto failure = expect(diagnostics.buffer_frames == 480,
                                    "Expected diagnostics buffer frames")) {
      return failure;
    }
    sar::platform::WasapiClockSnapshot synthetic_clock{
        .position = 1,
        .qpc_position_100ns = 2,
        .frequency = 3,
    };
    if (const auto failure = expect(!stream.read_clock(synthetic_clock),
                                    "Expected synthetic stream without native clock")) {
      return failure;
    }
    if (const auto failure = expect(synthetic_clock.position == 0 &&
                                        synthetic_clock.qpc_position_100ns == 0 &&
                                        synthetic_clock.frequency == 0,
                                    "Expected failed clock read to clear snapshot")) {
      return failure;
    }

    result = stream.start();
    if (const auto failure = expect(result.ok(), "Expected stream start success")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Started,
                                    "Expected started stream state")) {
      return failure;
    }
    if (const auto failure =
            expect(std::string(sar::platform::wasapi_stream_state_name(
                       stream.state())) == "started",
                   "Expected started stream state name")) {
      return failure;
    }
    diagnostics = stream.diagnostics();
    if (const auto failure =
            expect(diagnostics.state == sar::platform::WasapiStreamState::Started,
                   "Expected started diagnostics state")) {
      return failure;
    }

    sar::realtime::AudioBuffer render_buffer(2, 480);
    auto io_result = stream.render_once(render_buffer, 0);
    if (const auto failure = expect(!io_result.ok(),
                                    "Expected synthetic render pump failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(io_result, "native_stream_unavailable"),
                                    "Expected native_stream_unavailable error")) {
      return failure;
    }

    sar::realtime::AudioBuffer capture_buffer(2, 480);
    io_result = stream.capture_once(capture_buffer, 0);
    if (const auto failure = expect(!io_result.ok(),
                                    "Expected render stream capture pump failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(io_result, "wrong_stream_direction"),
                                    "Expected wrong_stream_direction error")) {
      return failure;
    }

    result = stream.stop();
    if (const auto failure = expect(result.ok(), "Expected stream stop success")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Open,
                                    "Expected open state after stop")) {
      return failure;
    }

    stream.close();
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected closed stream state")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto invalid_loopback_probe = make_probe();
    invalid_loopback_probe.mode = sar::platform::WasapiStreamMode::Loopback;
    const auto invalid_loopback_result = stream.open(invalid_loopback_probe);
    if (const auto failure = expect(!invalid_loopback_result.ok(),
                                    "Expected render loopback stream rejection")) {
      return failure;
    }
    if (const auto failure = expect(
            has_error_code(invalid_loopback_result, "invalid_loopback_direction"),
            "Expected invalid loopback direction stream error")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto loopback_probe = make_probe();
    loopback_probe.direction = sar::platform::WasapiStreamDirection::Capture;
    loopback_probe.mode = sar::platform::WasapiStreamMode::Loopback;
    const auto open_result = stream.open(std::move(loopback_probe));
    if (const auto failure = expect(open_result.ok(),
                                    "Expected synthetic loopback stream open")) {
      return failure;
    }
    if (const auto failure = expect(stream.diagnostics().mode ==
                                        sar::platform::WasapiStreamMode::Loopback,
                                    "Expected loopback diagnostics mode")) {
      return failure;
    }
    const auto start_result = stream.start();
    if (const auto failure = expect(start_result.ok(),
                                    "Expected synthetic loopback stream start")) {
      return failure;
    }
    sar::realtime::AudioBuffer capture_buffer(2, 480);
    const auto capture_result = stream.capture_once(capture_buffer, 0);
    if (const auto failure = expect(!capture_result.ok(),
                                    "Expected synthetic loopback without native client")) {
      return failure;
    }
    stream.close();
  }

  {
    auto invalid_encoding_probe = make_probe();
    invalid_encoding_probe.device_id =
        std::string(1, static_cast<char>(0xFF));
    const auto result =
        sar::platform::open_wasapi_stream_shell(std::move(invalid_encoding_probe));
    if (const auto failure = expect(!result.ok(),
                                    "Expected invalid native device ID rejection")) {
      return failure;
    }
    if (const auto failure = expect(
            result.errors().front().code == "invalid_device_id_encoding",
            "Expected invalid native device ID encoding error")) {
      return failure;
    }
  }

  {
    const auto result = sar::platform::open_wasapi_stream_shell(make_probe());
    if (const auto failure = expect(!result.ok(),
                                    "Expected missing selected device rejection")) {
      return failure;
    }
    if (const auto failure = expect(
            result.errors().front().code == "wasapi_device_lookup_failed",
            "Expected selected device lookup error")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto invalid_probe = make_probe();
    invalid_probe.buffer_frames = 0;
    invalid_probe.mix_format.bits_per_sample = 0;
    invalid_probe.mix_format.sample_format = sar::platform::AudioSampleFormat::Unknown;
    invalid_probe.mix_format.frames_per_block = 240;
    invalid_probe.device_id.clear();
    invalid_probe.device_label.clear();
    invalid_probe.mix_format.sample_rate = 0;
    invalid_probe.mix_format.channels = 0;
    invalid_probe.default_period_100ns = 0;
    invalid_probe.minimum_period_100ns = 0;
    const auto result = stream.open(invalid_probe);
    if (const auto failure = expect(!result.ok(), "Expected invalid probe failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_id"),
                                    "Expected empty_device_id error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "empty_device_label"),
                                    "Expected empty_device_label error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_sample_rate"),
                                    "Expected invalid_sample_rate error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_channel_count"),
                                    "Expected invalid_channel_count error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_buffer_frames"),
                                    "Expected invalid_buffer_frames error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_bits_per_sample"),
                                    "Expected invalid_bits_per_sample error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unsupported_sample_format"),
                                    "Expected unsupported_sample_format error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "frames_per_block_mismatch"),
                                    "Expected frames_per_block_mismatch error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_device_period"),
                                    "Expected invalid_device_period error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_minimum_device_period"),
                                    "Expected invalid_minimum_device_period error")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected invalid probe to leave stream closed")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto invalid_probe = make_probe();
    invalid_probe.minimum_period_100ns = invalid_probe.default_period_100ns + 1;
    const auto result = stream.open(invalid_probe);
    if (const auto failure =
            expect(!result.ok(), "Expected invalid period ordering failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_device_period_order"),
                                    "Expected invalid_device_period_order error")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected invalid period to leave stream closed")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto invalid_probe = make_probe();
    invalid_probe.mix_format.frames_per_block = 0;
    const auto result = stream.open(invalid_probe);
    if (const auto failure =
            expect(!result.ok(), "Expected invalid block size failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_frames_per_block"),
                                    "Expected invalid_frames_per_block error")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected invalid block size to leave stream closed")) {
      return failure;
    }
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto first_open = stream.open(make_probe());
    if (const auto failure = expect(first_open.ok(), "Expected first stream open success")) {
      return failure;
    }
    auto second_open = stream.open(make_probe());
    if (const auto failure = expect(!second_open.ok(), "Expected duplicate stream open failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(second_open, "stream_already_open"),
                                    "Expected stream_already_open error")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Open,
                                    "Expected duplicate open to preserve open state")) {
      return failure;
    }
    stream.close();
  }

  {
    sar::platform::WindowsWasapiStream stream;
    auto stop_result = stream.stop();
    if (const auto failure = expect(!stop_result.ok(), "Expected stop-before-open failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(stop_result, "stream_not_started"),
                                    "Expected stream_not_started before open")) {
      return failure;
    }

    auto open_result = stream.open(make_probe());
    if (const auto failure = expect(open_result.ok(), "Expected stream open before stop")) {
      return failure;
    }
    stop_result = stream.stop();
    if (const auto failure = expect(stop_result.ok(), "Expected stop-open stream success")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Open,
                                    "Expected stop-open stream to stay open")) {
      return failure;
    }
  }

  if (has_default_output_device()) {
    auto result = sar::platform::open_default_wasapi_stream_shell(
        sar::platform::WasapiStreamDirection::Render);
    if (!result.ok()) {
      for (const auto& error : result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure =
            expect(result.stream().state() == sar::platform::WasapiStreamState::Open,
                   "Expected default stream shell to open")) {
      return failure;
    }
    auto stream = result.take_stream();
    auto start_result = stream.start();
    if (!start_result.ok()) {
      for (const auto& error : start_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure =
            expect(stream.state() == sar::platform::WasapiStreamState::Started,
                   "Expected default stream shell to start")) {
      return failure;
    }
    sar::platform::WasapiClockSnapshot clock_snapshot;
    if (const auto failure = expect(stream.read_clock(clock_snapshot),
                                    "Expected native WASAPI clock snapshot")) {
      return failure;
    }
    if (const auto failure = expect(clock_snapshot.frequency > 0,
                                    "Expected native WASAPI clock frequency")) {
      return failure;
    }
    sar::realtime::AudioBuffer render_buffer(stream.probe().mix_format.channels,
                                             stream.probe().buffer_frames);
    auto io_result = stream.render_once(render_buffer, 0);
    if (!io_result.ok()) {
      for (const auto& error : io_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    auto stop_result = stream.stop();
    if (!stop_result.ok()) {
      for (const auto& error : stop_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure =
            expect(stream.state() == sar::platform::WasapiStreamState::Open,
                   "Expected default stream shell to stop")) {
      return failure;
    }

    auto selected_result =
        sar::platform::open_wasapi_stream_shell(stream.probe());
    if (!selected_result.ok()) {
      return 1;
    }
    if (const auto failure = expect(selected_result.stream().probe().device_id ==
                                        stream.probe().device_id,
                                    "Expected selected native stream device ID")) {
      return failure;
    }

    auto loopback_result = sar::platform::open_default_wasapi_stream_shell(
        sar::platform::WasapiStreamDirection::Capture,
        sar::platform::WasapiStreamMode::Loopback);
    if (!loopback_result.ok()) {
      for (const auto& error : loopback_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    auto loopback_stream = loopback_result.take_stream();
    if (const auto failure = expect(loopback_stream.probe().mode ==
                                        sar::platform::WasapiStreamMode::Loopback,
                                    "Expected native loopback stream mode")) {
      return failure;
    }
    start_result = loopback_stream.start();
    if (!start_result.ok()) {
      return 1;
    }
    sar::realtime::AudioBuffer loopback_buffer(
        loopback_stream.probe().mix_format.channels,
        loopback_stream.probe().buffer_frames);
    io_result = loopback_stream.capture_once(loopback_buffer, 0);
    if (!io_result.ok()) {
      return 1;
    }
    stop_result = loopback_stream.stop();
    if (!stop_result.ok()) {
      return 1;
    }
  }

  std::cout << "Windows WASAPI stream smoke test passed\n";
  return 0;
}
