#include "core/platform/wasapi_endpoint_selection_policy.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using sar::platform::AudioBackendKind;
using sar::platform::AudioDeviceDescriptor;
using sar::platform::AudioDeviceDirection;
using sar::platform::AudioDeviceListResult;
using sar::platform::WasapiDefaultEndpointGenerations;
using sar::platform::WasapiEndpointDirection;
using sar::platform::WasapiEndpointSelection;
using sar::platform::WasapiEndpointSelectionMode;
using sar::platform::WasapiEndpointSelectionPolicy;

int expect(bool condition, const char* message) {
  if (condition) {
    return 0;
  }
  std::cerr << message << '\n';
  return 1;
}

AudioDeviceDescriptor device(std::string id,
                             AudioDeviceDirection direction,
                             bool is_default = false) {
  AudioDeviceDescriptor descriptor;
  descriptor.id = std::move(id);
  descriptor.label = descriptor.id;
  descriptor.backend = AudioBackendKind::Wasapi;
  descriptor.direction = direction;
  descriptor.is_default = is_default;
  return descriptor;
}

}  // namespace

int main() {
  {
    bool rejected = false;
    try {
      static_cast<void>(WasapiEndpointSelection::pinned_device_id({}));
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    if (expect(rejected, "Empty pinned endpoint ID must be rejected")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy;
    policy.mark_opened({10, 20});

    auto reopen = policy.reopen_requirements({11, 20});
    if (expect(reopen.capture, "Capture default change must require reopen") ||
        expect(!reopen.render,
               "Unchanged render default must not require reopen")) {
      return 1;
    }

    reopen = policy.reopen_requirements({10, 21});
    if (expect(!reopen.capture,
               "Unchanged capture default must not require reopen") ||
        expect(reopen.render, "Render default change must require reopen")) {
      return 1;
    }

    policy.mark_opened(WasapiEndpointDirection::Render, 21);
    if (expect(!policy.reopen_required(WasapiEndpointDirection::Render, 21),
               "Successful reopen must accept the new render generation")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::follow_default(),
        WasapiEndpointSelection::pinned_device_id("render-pinned"));
    policy.mark_opened({100, 200});

    const auto reopen = policy.reopen_requirements({101, 999});
    if (expect(reopen.capture,
               "FollowDefault capture must track its generation") ||
        expect(!reopen.render,
               "Pinned render must ignore unrelated default changes") ||
        expect(policy.selection(WasapiEndpointDirection::Capture).mode ==
                   WasapiEndpointSelectionMode::FollowDefault,
               "Capture selection must remain independent") ||
        expect(policy.selection(WasapiEndpointDirection::Render).device_id ==
                   "render-pinned",
               "Render pinned ID must remain independent")) {
      return 1;
    }
  }

  const std::vector<AudioDeviceDescriptor> devices = {
      device("capture-default", AudioDeviceDirection::Input, true),
      device("capture-pinned", AudioDeviceDirection::Input),
      device("render-default", AudioDeviceDirection::Output, true),
      device("render-pinned", AudioDeviceDirection::Output),
  };

  {
    WasapiEndpointSelectionPolicy policy;
    const auto discovery = AudioDeviceListResult::success(devices);
    const auto capture =
        policy.resolve(WasapiEndpointDirection::Capture, discovery);
    if (expect(capture.ok(), "Successful discovery must resolve") ||
        expect(capture.device_id() == "capture-default",
               "Successful discovery must return the stable endpoint ID")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::follow_default(),
        WasapiEndpointSelection::pinned_device_id("render-pinned"));
    const auto pair = policy.resolve_pair(devices);
    if (expect(pair.ok(), "Mixed endpoint pair must resolve") ||
        expect(pair.endpoints().capture_device_id == "capture-default",
               "FollowDefault capture must select its directional default") ||
        expect(pair.endpoints().render_device_id == "render-pinned",
               "Pinned render must select the explicit device ID")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::pinned_device_id("capture-pinned"),
        WasapiEndpointSelection::pinned_device_id("render-pinned"));
    const auto pair = policy.resolve_pair(devices);
    if (expect(pair.ok(), "Two pinned endpoints must resolve as a pair") ||
        expect(pair.endpoints().capture_device_id == "capture-pinned",
               "Pinned pair must preserve the capture device ID") ||
        expect(pair.endpoints().render_device_id == "render-pinned",
               "Pinned pair must preserve the render device ID")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::pinned_device_id("missing-capture"),
        WasapiEndpointSelection::follow_default());
    const auto first = policy.resolve(WasapiEndpointDirection::Capture, devices);
    const auto second = policy.resolve(WasapiEndpointDirection::Capture, devices);
    if (expect(!first.ok(), "Unavailable pinned endpoint must fail") ||
        expect(first.errors().size() == 1,
               "Unavailable pinned endpoint must report one stable error") ||
        expect(first.errors()[0].code ==
                   "wasapi_pinned_endpoint_unavailable",
               "Unavailable pinned endpoint must use the stable error code") ||
        expect(first.errors()[0].direction == WasapiEndpointDirection::Capture,
               "Unavailable pinned endpoint must report its direction") ||
        expect(first.errors()[0].device_id == "missing-capture",
               "Unavailable pinned endpoint must report its explicit ID") ||
        expect(second.errors()[0].code == first.errors()[0].code &&
                   second.errors()[0].message == first.errors()[0].message,
               "Repeated pinned endpoint failures must be stable")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy;
    const std::vector<AudioDeviceDescriptor> no_capture_default = {
        device("capture-not-default", AudioDeviceDirection::Input),
    };
    const auto first = policy.resolve(WasapiEndpointDirection::Capture,
                                      no_capture_default);
    const auto second = policy.resolve(WasapiEndpointDirection::Capture,
                                       no_capture_default);
    if (expect(!first.ok(), "Unavailable default endpoint must fail") ||
        expect(first.errors().size() == 1,
               "Unavailable default endpoint must report one stable error") ||
        expect(first.errors()[0].code ==
                   "wasapi_default_endpoint_unavailable",
               "Unavailable default endpoint must use the stable error code") ||
        expect(second.errors()[0].code == first.errors()[0].code &&
                   second.errors()[0].message == first.errors()[0].message,
               "Repeated default endpoint failures must be stable")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::pinned_device_id("missing-capture"),
        WasapiEndpointSelection::follow_default());
    const auto first = policy.resolve_pair(devices);
    const auto second = policy.resolve_pair(devices);
    if (expect(!first.ok(), "Pair with one missing endpoint must fail") ||
        expect(first.endpoints().capture_device_id.empty(),
               "Missing capture endpoint must not produce an ID") ||
        expect(first.endpoints().render_device_id == "render-default",
               "Failed pair must retain the resolved render ID") ||
        expect(first.errors().size() == 1,
               "One missing endpoint must report one directional error") ||
        expect(first.errors()[0].direction ==
                   WasapiEndpointDirection::Capture,
               "Missing capture endpoint must retain its direction") ||
        expect(second.endpoints().render_device_id ==
                   first.endpoints().render_device_id &&
                   second.errors()[0].message == first.errors()[0].message,
               "Repeated partial pair failures must be stable")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::pinned_device_id("missing-capture"),
        WasapiEndpointSelection::pinned_device_id("missing-render"));
    const auto pair = policy.resolve_pair(devices);
    if (expect(!pair.ok(), "Pair with two missing endpoints must fail") ||
        expect(pair.errors().size() == 2,
               "Two missing endpoints must retain both errors") ||
        expect(pair.errors()[0].direction ==
                   WasapiEndpointDirection::Capture &&
                   pair.errors()[1].direction ==
                       WasapiEndpointDirection::Render,
               "Pair errors must have stable capture-render ordering") ||
        expect(pair.errors()[0].device_id == "missing-capture" &&
                   pair.errors()[1].device_id == "missing-render",
               "Pair errors must retain both requested device IDs")) {
      return 1;
    }
  }

  {
    WasapiEndpointSelectionPolicy policy(
        WasapiEndpointSelection::pinned_device_id("capture-pinned"),
        WasapiEndpointSelection::pinned_device_id("render-pinned"));
    const auto discovery = AudioDeviceListResult::failure({
        {"wasapi_enum_failed", "Machine-specific discovery detail."},
    });
    const auto first = policy.resolve_pair(discovery);
    const auto second = policy.resolve_pair(discovery);
    if (expect(!first.ok(), "Failed discovery must not resolve a pair") ||
        expect(first.errors().size() == 2,
               "Failed discovery must report both directional errors") ||
        expect(first.errors()[0].code == "wasapi_endpoint_discovery_failed" &&
                   first.errors()[1].code ==
                       "wasapi_endpoint_discovery_failed",
               "Failed discovery must use the stable integration error code") ||
        expect(first.errors()[0].direction ==
                   WasapiEndpointDirection::Capture &&
                   first.errors()[1].direction ==
                       WasapiEndpointDirection::Render,
               "Failed discovery must preserve both endpoint directions") ||
        expect(first.errors()[0].device_id == "capture-pinned" &&
                   first.errors()[1].device_id == "render-pinned",
               "Failed discovery must preserve both requested IDs") ||
        expect(second.errors()[0].message == first.errors()[0].message &&
                   second.errors()[1].message == first.errors()[1].message,
               "Repeated discovery failures must be stable")) {
      return 1;
    }
  }

  std::cout << "WASAPI endpoint selection policy smoke test passed\n";
  return 0;
}
