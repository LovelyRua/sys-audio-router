#include "core/platform/windows_wasapi_device_provider.h"

#include <iostream>
#include <string>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

int verify_device_contract(const sar::platform::AudioDeviceDescriptor& device) {
  if (const auto failure = expect(device.backend == sar::platform::AudioBackendKind::Wasapi,
                                  "Expected WASAPI device backend")) {
    return failure;
  }
  if (const auto failure = expect(!device.id.empty(), "Expected WASAPI device id")) {
    return failure;
  }
  if (const auto failure = expect(!device.label.empty(), "Expected WASAPI device label")) {
    return failure;
  }
  if (const auto failure = expect(!device.formats.empty(), "Expected WASAPI device format")) {
    return failure;
  }

  for (const auto& format : device.formats) {
    if (const auto failure = expect(format.sample_rate > 0,
                                    "Expected WASAPI device sample rate")) {
      return failure;
    }
    if (const auto failure = expect(format.channels > 0,
                                    "Expected WASAPI device channel count")) {
      return failure;
    }
    if (const auto failure = expect(format.frames_per_block > 0,
                                    "Expected WASAPI device block size")) {
      return failure;
    }
    if (const auto failure = expect(format.bits_per_sample > 0,
                                    "Expected WASAPI device bit depth")) {
      return failure;
    }
    if (const auto failure =
            expect(format.sample_format != sar::platform::AudioSampleFormat::Unknown,
                   "Expected WASAPI device sample format")) {
      return failure;
    }
    if (const auto failure =
            expect(format.valid_bits_per_sample <= format.bits_per_sample,
                   "Expected WASAPI valid bits not to exceed container bits")) {
      return failure;
    }
  }

  return 0;
}

}  // namespace

int main() {
  sar::platform::WindowsWasapiDeviceProvider provider;
  if (provider.backend() != sar::platform::AudioBackendKind::Wasapi) {
    std::cerr << "Expected WASAPI backend kind\n";
    return 1;
  }

  const auto result = provider.list_devices();
  if (!result.ok()) {
    for (const auto& error : result.errors()) {
      std::cerr << error.code << ": " << error.message << '\n';
    }
    return 1;
  }

  bool saw_default_input = false;
  bool saw_default_output = false;
  std::string input_id;
  std::string output_id;
  for (const auto& device : result.devices()) {
    if (const auto failure = verify_device_contract(device)) {
      return failure;
    }
    if (device.is_default && device.direction == sar::platform::AudioDeviceDirection::Input) {
      saw_default_input = true;
    }
    if (device.is_default && device.direction == sar::platform::AudioDeviceDirection::Output) {
      saw_default_output = true;
    }
    if (input_id.empty() &&
        device.direction == sar::platform::AudioDeviceDirection::Input) {
      input_id = device.id;
    }
    if (output_id.empty() &&
        device.direction == sar::platform::AudioDeviceDirection::Output) {
      output_id = device.id;
    }
  }

  const sar::platform::WasapiEndpointSelectionPolicy pinned_policy(
      input_id.empty()
          ? sar::platform::WasapiEndpointSelection::follow_default()
          : sar::platform::WasapiEndpointSelection::pinned_device_id(input_id),
      output_id.empty()
          ? sar::platform::WasapiEndpointSelection::follow_default()
          : sar::platform::WasapiEndpointSelection::pinned_device_id(output_id));
  if (!input_id.empty() && !output_id.empty()) {
    const auto pinned = provider.resolve_endpoint_pair(pinned_policy);
    if (expect(pinned.ok(), "Expected provider to resolve endpoint pair") ||
        expect(pinned.endpoints().capture_device_id == input_id,
               "Expected provider to resolve pinned capture endpoint") ||
        expect(pinned.endpoints().render_device_id == output_id,
               "Expected provider to resolve pinned render endpoint")) {
      return 1;
    }
  }

  const sar::platform::WasapiEndpointSelectionPolicy missing_policy(
      sar::platform::WasapiEndpointSelection::pinned_device_id(
          "sar-missing-capture-endpoint"),
      sar::platform::WasapiEndpointSelection::follow_default());
  const auto missing = provider.resolve_endpoint_pair(missing_policy);
  const auto missing_again = provider.resolve_endpoint_pair(missing_policy);
  if (expect(!missing.ok(), "Expected missing pinned endpoint to fail") ||
      expect(!missing.errors().empty() &&
                 missing.errors()[0].code ==
                     "wasapi_pinned_endpoint_unavailable",
             "Expected stable missing pinned endpoint error") ||
      expect(!saw_default_output ||
                 !missing.endpoints().render_device_id.empty(),
             "Expected failed pair to retain resolved default render ID") ||
      expect(!missing_again.ok() &&
                 missing_again.errors()[0].code == missing.errors()[0].code &&
                 missing_again.errors()[0].message ==
                     missing.errors()[0].message,
             "Expected repeated pinned endpoint failure to be stable")) {
    return 1;
  }

  const sar::platform::WasapiEndpointSelectionPolicy default_policy;
  if (saw_default_input && saw_default_output) {
    const auto resolved = provider.resolve_endpoint_pair(default_policy);
    if (expect(resolved.ok() &&
                   !resolved.endpoints().capture_device_id.empty() &&
                   !resolved.endpoints().render_device_id.empty(),
               "Expected provider to resolve both default endpoints")) {
      return 1;
    }
  }

  std::cout << "Windows WASAPI device smoke test passed with "
            << result.devices().size() << " active endpoints";
  if (saw_default_input) {
    std::cout << ", default input";
  }
  if (saw_default_output) {
    std::cout << ", default output";
  }
  std::cout << '\n';
  return 0;
}
