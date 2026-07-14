#pragma once

#include "core/platform/audio_device.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sar::platform {

enum class WasapiEndpointDirection {
  Capture,
  Render,
};

enum class WasapiEndpointSelectionMode {
  FollowDefault,
  PinnedDeviceId,
};

struct WasapiEndpointSelection {
  WasapiEndpointSelectionMode mode = WasapiEndpointSelectionMode::FollowDefault;
  std::string device_id;

  [[nodiscard]] static WasapiEndpointSelection follow_default();
  [[nodiscard]] static WasapiEndpointSelection pinned_device_id(
      std::string device_id);
};

struct WasapiDefaultEndpointGenerations {
  std::uint64_t capture = 0;
  std::uint64_t render = 0;
};

struct WasapiEndpointReopenRequirements {
  bool capture = false;
  bool render = false;
};

struct WasapiEndpointSelectionError {
  std::string code;
  std::string message;
  WasapiEndpointDirection direction = WasapiEndpointDirection::Render;
  std::string device_id;
};

class WasapiEndpointResolutionResult {
 public:
  static WasapiEndpointResolutionResult success(std::string device_id);
  static WasapiEndpointResolutionResult failure(
      WasapiEndpointSelectionError error);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] const std::string& device_id() const noexcept;
  [[nodiscard]] const std::vector<WasapiEndpointSelectionError>& errors()
      const noexcept;

 private:
  WasapiEndpointResolutionResult(
      std::string device_id,
      std::vector<WasapiEndpointSelectionError> errors);

  std::string device_id_;
  std::vector<WasapiEndpointSelectionError> errors_;
};

class WasapiEndpointSelectionPolicy {
 public:
  WasapiEndpointSelectionPolicy();
  WasapiEndpointSelectionPolicy(WasapiEndpointSelection capture,
                                WasapiEndpointSelection render);

  [[nodiscard]] const WasapiEndpointSelection& selection(
      WasapiEndpointDirection direction) const noexcept;

  // Generations identify the defaults used by the last successful open.
  void mark_opened(WasapiDefaultEndpointGenerations generations) noexcept;
  void mark_opened(WasapiEndpointDirection direction,
                   std::uint64_t generation) noexcept;

  [[nodiscard]] bool reopen_required(
      WasapiEndpointDirection direction,
      std::uint64_t current_default_generation) const noexcept;
  [[nodiscard]] WasapiEndpointReopenRequirements reopen_requirements(
      WasapiDefaultEndpointGenerations current) const noexcept;

  [[nodiscard]] WasapiEndpointResolutionResult resolve(
      WasapiEndpointDirection direction,
      const std::vector<AudioDeviceDescriptor>& devices) const;

 private:
  WasapiEndpointSelection capture_;
  WasapiEndpointSelection render_;
  WasapiDefaultEndpointGenerations opened_generations_;
};

}  // namespace sar::platform
