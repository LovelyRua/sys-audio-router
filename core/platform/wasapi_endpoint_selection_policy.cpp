#include "core/platform/wasapi_endpoint_selection_policy.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace sar::platform {

namespace {

const char* direction_name(WasapiEndpointDirection direction) noexcept {
  return direction == WasapiEndpointDirection::Capture ? "capture" : "render";
}

bool direction_matches(const AudioDeviceDescriptor& device,
                       WasapiEndpointDirection direction) noexcept {
  if (device.backend != AudioBackendKind::Wasapi) {
    return false;
  }
  if (device.direction == AudioDeviceDirection::Duplex) {
    return true;
  }
  return direction == WasapiEndpointDirection::Capture
             ? device.direction == AudioDeviceDirection::Input
             : device.direction == AudioDeviceDirection::Output;
}

WasapiEndpointSelectionError make_unavailable_error(
    WasapiEndpointDirection direction,
    const WasapiEndpointSelection& selection) {
  if (selection.mode == WasapiEndpointSelectionMode::PinnedDeviceId) {
    return {
        "wasapi_pinned_endpoint_unavailable",
        "Pinned WASAPI " + std::string(direction_name(direction)) +
            " endpoint '" + selection.device_id + "' is unavailable.",
        direction,
        selection.device_id,
    };
  }
  return {
      "wasapi_default_endpoint_unavailable",
      "Default WASAPI " + std::string(direction_name(direction)) +
          " endpoint is unavailable.",
      direction,
      {},
  };
}

WasapiEndpointSelectionError make_discovery_error(
    WasapiEndpointDirection direction,
    const WasapiEndpointSelection& selection) {
  return {
      "wasapi_endpoint_discovery_failed",
      "WASAPI " + std::string(direction_name(direction)) +
          " endpoint discovery failed.",
      direction,
      selection.mode == WasapiEndpointSelectionMode::PinnedDeviceId
          ? selection.device_id
          : std::string{},
  };
}

}  // namespace

WasapiEndpointSelection WasapiEndpointSelection::follow_default() {
  return {};
}

WasapiEndpointSelection WasapiEndpointSelection::pinned_device_id(
    std::string device_id) {
  if (device_id.empty()) {
    throw std::invalid_argument("Pinned WASAPI device ID must not be empty");
  }
  return {WasapiEndpointSelectionMode::PinnedDeviceId, std::move(device_id)};
}

WasapiEndpointResolutionResult WasapiEndpointResolutionResult::success(
    std::string device_id) {
  return {std::move(device_id), {}};
}

WasapiEndpointResolutionResult WasapiEndpointResolutionResult::failure(
    WasapiEndpointSelectionError error) {
  std::vector<WasapiEndpointSelectionError> errors;
  errors.push_back(std::move(error));
  return {{}, std::move(errors)};
}

bool WasapiEndpointResolutionResult::ok() const noexcept {
  return errors_.empty();
}

const std::string& WasapiEndpointResolutionResult::device_id() const noexcept {
  return device_id_;
}

const std::vector<WasapiEndpointSelectionError>&
WasapiEndpointResolutionResult::errors() const noexcept {
  return errors_;
}

WasapiEndpointResolutionResult::WasapiEndpointResolutionResult(
    std::string device_id,
    std::vector<WasapiEndpointSelectionError> errors)
    : device_id_(std::move(device_id)), errors_(std::move(errors)) {}

bool WasapiEndpointPairResolutionResult::ok() const noexcept {
  return errors_.empty();
}

const WasapiEndpointPair& WasapiEndpointPairResolutionResult::endpoints()
    const noexcept {
  return endpoints_;
}

const std::vector<WasapiEndpointSelectionError>&
WasapiEndpointPairResolutionResult::errors() const noexcept {
  return errors_;
}

WasapiEndpointPairResolutionResult::WasapiEndpointPairResolutionResult(
    WasapiEndpointPair endpoints,
    std::vector<WasapiEndpointSelectionError> errors)
    : endpoints_(std::move(endpoints)), errors_(std::move(errors)) {}

WasapiEndpointSelectionPolicy::WasapiEndpointSelectionPolicy()
    : WasapiEndpointSelectionPolicy(WasapiEndpointSelection::follow_default(),
                                    WasapiEndpointSelection::follow_default()) {}

WasapiEndpointSelectionPolicy::WasapiEndpointSelectionPolicy(
    WasapiEndpointSelection capture,
    WasapiEndpointSelection render)
    : capture_(std::move(capture)), render_(std::move(render)) {}

const WasapiEndpointSelection& WasapiEndpointSelectionPolicy::selection(
    WasapiEndpointDirection direction) const noexcept {
  return direction == WasapiEndpointDirection::Capture ? capture_ : render_;
}

void WasapiEndpointSelectionPolicy::mark_opened(
    WasapiDefaultEndpointGenerations generations) noexcept {
  opened_generations_ = generations;
}

void WasapiEndpointSelectionPolicy::mark_opened(
    WasapiEndpointDirection direction,
    std::uint64_t generation) noexcept {
  if (direction == WasapiEndpointDirection::Capture) {
    opened_generations_.capture = generation;
  } else {
    opened_generations_.render = generation;
  }
}

bool WasapiEndpointSelectionPolicy::reopen_required(
    WasapiEndpointDirection direction,
    std::uint64_t current_default_generation) const noexcept {
  const auto& endpoint_selection = selection(direction);
  if (endpoint_selection.mode != WasapiEndpointSelectionMode::FollowDefault) {
    return false;
  }
  const auto opened_generation =
      direction == WasapiEndpointDirection::Capture
          ? opened_generations_.capture
          : opened_generations_.render;
  return current_default_generation != opened_generation;
}

WasapiEndpointReopenRequirements
WasapiEndpointSelectionPolicy::reopen_requirements(
    WasapiDefaultEndpointGenerations current) const noexcept {
  return {
      reopen_required(WasapiEndpointDirection::Capture, current.capture),
      reopen_required(WasapiEndpointDirection::Render, current.render),
  };
}

WasapiEndpointResolutionResult WasapiEndpointSelectionPolicy::resolve(
    WasapiEndpointDirection direction,
    const std::vector<AudioDeviceDescriptor>& devices) const {
  const auto& endpoint_selection = selection(direction);
  const auto endpoint = std::find_if(
      devices.begin(), devices.end(), [&](const auto& device) {
        if (!direction_matches(device, direction)) {
          return false;
        }
        return endpoint_selection.mode ==
                       WasapiEndpointSelectionMode::FollowDefault
                   ? device.is_default
                   : device.id == endpoint_selection.device_id;
      });
  if (endpoint == devices.end()) {
    return WasapiEndpointResolutionResult::failure(
        make_unavailable_error(direction, endpoint_selection));
  }
  return WasapiEndpointResolutionResult::success(endpoint->id);
}

WasapiEndpointResolutionResult WasapiEndpointSelectionPolicy::resolve(
    WasapiEndpointDirection direction,
    const AudioDeviceListResult& discovery) const {
  if (!discovery.ok()) {
    return WasapiEndpointResolutionResult::failure(
        make_discovery_error(direction, selection(direction)));
  }
  return resolve(direction, discovery.devices());
}

WasapiEndpointPairResolutionResult WasapiEndpointSelectionPolicy::resolve_pair(
    const std::vector<AudioDeviceDescriptor>& devices) const {
  auto capture = resolve(WasapiEndpointDirection::Capture, devices);
  auto render = resolve(WasapiEndpointDirection::Render, devices);

  WasapiEndpointPair endpoints{
      capture.device_id(),
      render.device_id(),
  };
  std::vector<WasapiEndpointSelectionError> errors;
  errors.reserve(capture.errors().size() + render.errors().size());
  errors.insert(errors.end(), capture.errors().begin(), capture.errors().end());
  errors.insert(errors.end(), render.errors().begin(), render.errors().end());
  return {std::move(endpoints), std::move(errors)};
}

WasapiEndpointPairResolutionResult WasapiEndpointSelectionPolicy::resolve_pair(
    const AudioDeviceListResult& discovery) const {
  if (discovery.ok()) {
    return resolve_pair(discovery.devices());
  }

  std::vector<WasapiEndpointSelectionError> errors;
  errors.reserve(2);
  errors.push_back(make_discovery_error(WasapiEndpointDirection::Capture,
                                        capture_));
  errors.push_back(
      make_discovery_error(WasapiEndpointDirection::Render, render_));
  return {{}, std::move(errors)};
}

}  // namespace sar::platform
