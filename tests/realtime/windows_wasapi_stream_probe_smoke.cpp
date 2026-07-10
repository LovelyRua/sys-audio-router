#include "core/platform/windows_wasapi_device_provider.h"
#include "core/platform/windows_wasapi_stream_probe.h"

#include <iostream>
#include <string>

namespace {

struct DefaultEndpointAvailability {
  bool capture = false;
  bool render = false;
};

DefaultEndpointAvailability default_endpoint_availability() {
  DefaultEndpointAvailability availability;
  sar::platform::WindowsWasapiDeviceProvider provider;
  const auto result = provider.list_devices();
  if (!result.ok()) {
    return availability;
  }

  for (const auto& device : result.devices()) {
    if (!device.is_default) {
      continue;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Input) {
      availability.capture = true;
    }
    if (device.direction == sar::platform::AudioDeviceDirection::Output) {
      availability.render = true;
    }
  }
  return availability;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

int verify_probe_contract(const sar::platform::WasapiStreamProbe& probe,
                          sar::platform::WasapiStreamDirection direction,
                          sar::platform::WasapiStreamMode mode,
                          const char* label) {
  if (const auto failure = expect(probe.direction == direction,
                                  "Expected probed WASAPI direction")) {
    return failure;
  }
  if (const auto failure = expect(probe.mode == mode,
                                  "Expected probed WASAPI mode")) {
    return failure;
  }
  if (const auto failure = expect(!probe.device_id.empty(),
                                  "Expected probed WASAPI device id")) {
    return failure;
  }
  if (const auto failure = expect(!probe.device_label.empty(),
                                  "Expected probed WASAPI device label")) {
    return failure;
  }
  if (const auto failure = expect(probe.mix_format.sample_rate > 0,
                                  "Expected probed WASAPI sample rate")) {
    return failure;
  }
  if (const auto failure = expect(probe.mix_format.channels > 0,
                                  "Expected probed WASAPI channel count")) {
    return failure;
  }
  if (const auto failure = expect(probe.mix_format.bits_per_sample > 0,
                                  "Expected probed WASAPI bit depth")) {
    return failure;
  }
  if (const auto failure =
          expect(probe.mix_format.sample_format != sar::platform::AudioSampleFormat::Unknown,
                 "Expected supported probed WASAPI sample format")) {
    return failure;
  }
  if (const auto failure = expect(probe.buffer_frames > 0,
                                  "Expected probed WASAPI buffer frames")) {
    return failure;
  }
  if (const auto failure = expect(probe.mix_format.frames_per_block == probe.buffer_frames,
                                  "Expected probed frames-per-block to match buffer frames")) {
    return failure;
  }
  if (const auto failure = expect(probe.default_period_100ns > 0,
                                  "Expected probed WASAPI default period")) {
    return failure;
  }
  if (const auto failure = expect(probe.minimum_period_100ns > 0,
                                  "Expected probed WASAPI minimum period")) {
    return failure;
  }
  if (const auto failure = expect(probe.minimum_period_100ns <= probe.default_period_100ns,
                                  "Expected WASAPI minimum period not to exceed default period")) {
    return failure;
  }
  if (probe.mix_format.valid_bits_per_sample > probe.mix_format.bits_per_sample) {
    std::cerr << "Expected probed WASAPI valid bits not to exceed container bits\n";
    return 1;
  }

  std::cout << "Windows WASAPI " << label << " stream probe: "
            << probe.mix_format.sample_rate << " Hz, "
            << probe.mix_format.channels << " channels, "
            << probe.mix_format.bits_per_sample << " bit, "
            << probe.buffer_frames << " buffer frames\n";
  return 0;
}

}  // namespace

int main() {
  if (const auto failure =
          expect(std::string(sar::platform::wasapi_stream_direction_name(
                     sar::platform::WasapiStreamDirection::Render)) == "render",
                 "Expected render stream direction name")) {
    return failure;
  }
  if (const auto failure =
          expect(std::string(sar::platform::wasapi_stream_mode_name(
                     sar::platform::WasapiStreamMode::Endpoint)) == "endpoint",
                 "Expected endpoint stream mode name")) {
    return failure;
  }
  if (const auto failure =
          expect(std::string(sar::platform::wasapi_stream_mode_name(
                     sar::platform::WasapiStreamMode::Loopback)) == "loopback",
                 "Expected loopback stream mode name")) {
    return failure;
  }
  const auto invalid_loopback = sar::platform::probe_default_wasapi_stream(
      sar::platform::WasapiStreamDirection::Render,
      sar::platform::WasapiStreamMode::Loopback);
  if (const auto failure = expect(!invalid_loopback.ok(),
                                  "Expected render loopback probe rejection")) {
    return failure;
  }
  if (const auto failure =
          expect(invalid_loopback.errors().front().code ==
                     "invalid_loopback_direction",
                 "Expected invalid loopback direction error")) {
    return failure;
  }
  if (const auto failure =
          expect(std::string(sar::platform::wasapi_stream_direction_name(
                     sar::platform::WasapiStreamDirection::Capture)) == "capture",
                 "Expected capture stream direction name")) {
    return failure;
  }

  const auto availability = default_endpoint_availability();
  if (!availability.render) {
    std::cout << "Windows WASAPI stream probe skipped: no default output endpoint\n";
  } else {
    const auto result = sar::platform::probe_default_wasapi_stream(
        sar::platform::WasapiStreamDirection::Render);
    if (!result.ok()) {
      for (const auto& error : result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure = verify_probe_contract(result.probe(),
                                                   sar::platform::WasapiStreamDirection::Render,
                                                   sar::platform::WasapiStreamMode::Endpoint,
                                                   "render")) {
      return failure;
    }

    const auto loopback_result = sar::platform::probe_default_wasapi_stream(
        sar::platform::WasapiStreamDirection::Capture,
        sar::platform::WasapiStreamMode::Loopback);
    if (!loopback_result.ok()) {
      for (const auto& error : loopback_result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure = verify_probe_contract(
            loopback_result.probe(),
            sar::platform::WasapiStreamDirection::Capture,
            sar::platform::WasapiStreamMode::Loopback,
            "loopback capture")) {
      return failure;
    }
  }

  if (!availability.capture) {
    std::cout << "Windows WASAPI capture stream probe skipped: no default input endpoint\n";
  } else {
    const auto result = sar::platform::probe_default_wasapi_stream(
        sar::platform::WasapiStreamDirection::Capture);
    if (!result.ok()) {
      for (const auto& error : result.errors()) {
        std::cerr << error.code << ": " << error.message << '\n';
      }
      return 1;
    }
    if (const auto failure = verify_probe_contract(result.probe(),
                                                   sar::platform::WasapiStreamDirection::Capture,
                                                   sar::platform::WasapiStreamMode::Endpoint,
                                                   "capture")) {
      return failure;
    }
  }

  if (!availability.capture && !availability.render) {
    std::cout << "Windows WASAPI stream probe skipped: no default endpoints\n";
    return 0;
  }

  std::cout << "Windows WASAPI stream probe smoke test passed\n";
  return 0;
}
