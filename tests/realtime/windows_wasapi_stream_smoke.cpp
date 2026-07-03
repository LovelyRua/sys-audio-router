#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_stream.h"

#include <iostream>
#include <string>

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
    sar::platform::WindowsWasapiStream stream;
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Closed,
                                    "Expected initial closed stream state")) {
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

    result = stream.start();
    if (const auto failure = expect(result.ok(), "Expected stream start success")) {
      return failure;
    }
    if (const auto failure = expect(stream.state() == sar::platform::WasapiStreamState::Started,
                                    "Expected started stream state")) {
      return failure;
    }
    diagnostics = stream.diagnostics();
    if (const auto failure =
            expect(diagnostics.state == sar::platform::WasapiStreamState::Started,
                   "Expected started diagnostics state")) {
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
    auto invalid_probe = make_probe();
    invalid_probe.buffer_frames = 0;
    const auto result = stream.open(invalid_probe);
    if (const auto failure = expect(!result.ok(), "Expected invalid probe failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_buffer_frames"),
                                    "Expected invalid_buffer_frames error")) {
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
  }

  std::cout << "Windows WASAPI stream smoke test passed\n";
  return 0;
}
