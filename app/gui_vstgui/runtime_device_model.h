#pragma once

#include "core/control/control_command.h"
#include "core/platform/audio_device.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <vector>

namespace sar::gui_vstgui {

struct RuntimeEndpointDraft {
  control::AudioRuntimeEndpointConfiguration endpoint;
  std::string firstChannel;
  std::string channelCount;
  std::string sampleRate;
  std::string blockFrames;
};

class RuntimeDeviceDraft {
 public:
  void load(const control::AudioRuntimeConfiguration& configuration) {
    source_ = configuration;
    endpoints_.clear();
    for (const auto& endpoint : configuration.endpoints)
      endpoints_.push_back({endpoint, std::to_string(endpoint.first_channel),
                            std::to_string(endpoint.channel_count),
                            std::to_string(endpoint.sample_rate), std::to_string(endpoint.block_frames)});
    dirty_ = false;
  }

  bool editable() const {
    return source_.mode == control::AudioRuntimeMode::None ||
           source_.mode == control::AudioRuntimeMode::WasapiMatrix;
  }
  bool dirty() const { return dirty_; }
  bool hasPhysicalAsio() const {
    return std::any_of(endpoints_.begin(), endpoints_.end(), [](const auto& item) {
      return item.endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio;
    });
  }
  bool addPhysicalAsio() {
    if (!editable() || hasPhysicalAsio() ||
        endpoints_.size() + 2 > control::kMaximumAudioRuntimeEndpoints) return false;
    std::string group;
    for (std::size_t n = 1; n <= control::kMaximumAudioRuntimeEndpoints + 1; ++n) {
      auto candidate = "physical-asio-" + std::to_string(n);
      if (std::none_of(endpoints_.begin(), endpoints_.end(), [&](const auto& item) {
          return item.endpoint.endpoint_id == candidate + "-in" ||
                 item.endpoint.endpoint_id == candidate + "-out";
        })) { group = std::move(candidate); break; }
    }
    for (auto& item : endpoints_) item.endpoint.clock_master = false;
    for (const bool render : {false, true}) {
      control::AudioRuntimeEndpointConfiguration endpoint;
      endpoint.backend = control::AudioRuntimeEndpointBackend::PhysicalAsio;
      endpoint.device_group_id = group;
      endpoint.endpoint_id = group + (render ? "-out" : "-in");
      endpoint.direction = render ? control::AudioRuntimeEndpointDirection::Render
                                  : control::AudioRuntimeEndpointDirection::Capture;
      endpoint.clock_master = render;
      endpoint.channel_count = 2;
      endpoint.sample_rate = 48000;
      endpoint.block_frames = 128;
      endpoints_.push_back({std::move(endpoint), "0", "2", "48000", "128"});
    }
    dirty_ = true;
    return true;
  }
  bool setTiming(std::size_t index, const std::string& rate, const std::string& frames) {
    const auto item = at(index);
    if (!editable() || !item ||
        item->endpoint.backend != control::AudioRuntimeEndpointBackend::PhysicalAsio) return false;
    const auto group = item->endpoint.device_group_id;
    for (auto& candidate : endpoints_)
      if (candidate.endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio &&
          candidate.endpoint.device_group_id == group) {
        candidate.sampleRate = rate;
        candidate.blockFrames = frames;
      }
    dirty_ = true;
    return true;
  }
  bool removePhysicalAsio(std::size_t index) {
    const auto item = at(index);
    if (!editable() || !item ||
        item->endpoint.backend != control::AudioRuntimeEndpointBackend::PhysicalAsio) return false;
    const auto group = item->endpoint.device_group_id;
    std::erase_if(endpoints_, [&](const auto& candidate) {
      return candidate.endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio &&
             candidate.endpoint.device_group_id == group;
    });
    if (std::none_of(endpoints_.begin(), endpoints_.end(), [](const auto& candidate) {
          return candidate.endpoint.clock_master;
        })) {
      const auto render = std::find_if(endpoints_.begin(), endpoints_.end(), [](const auto& candidate) {
        return candidate.endpoint.direction == control::AudioRuntimeEndpointDirection::Render;
      });
      if (render != endpoints_.end()) render->endpoint.clock_master = true;
    }
    dirty_ = true;
    return true;
  }
  bool bindDevice(std::size_t index, const std::string& id) {
    auto item = at(index);
    if (!editable() || !item) return false;
    const auto group = item->endpoint.device_group_id;
    const auto backend = item->endpoint.backend;
    for (auto& candidate : endpoints_)
      if (&candidate == item ||
          (backend == control::AudioRuntimeEndpointBackend::PhysicalAsio &&
           candidate.endpoint.backend == backend && candidate.endpoint.device_group_id == group))
        candidate.endpoint.device_id = id;
    dirty_ = true;
    return true;
  }
  void changed() { dirty_ = true; }
  const auto& endpoints() const { return endpoints_; }
  RuntimeEndpointDraft* at(std::size_t index) {
    return index < endpoints_.size() ? &endpoints_[index] : nullptr;
  }
  void newMatrix() {
    load({});
    source_.mode = control::AudioRuntimeMode::WasapiMatrix;
    dirty_ = true;
  }

  bool add(control::AudioRuntimeEndpointDirection direction) {
    if (!editable() || endpoints_.size() >= control::kMaximumAudioRuntimeEndpoints) return false;
    control::AudioRuntimeEndpointConfiguration endpoint;
    endpoint.direction = direction;
    endpoint.channel_count = 2;
    const bool render = direction == control::AudioRuntimeEndpointDirection::Render;
    endpoint.clock_master = render && std::none_of(endpoints_.begin(), endpoints_.end(),
        [](const auto& item) { return item.endpoint.clock_master; });
    for (std::size_t i = 1; i <= control::kMaximumAudioRuntimeEndpoints + 1; ++i) {
      auto id = std::string(render ? "render-" : "capture-") + std::to_string(i);
      if (std::none_of(endpoints_.begin(), endpoints_.end(), [&](const auto& item) {
            return item.endpoint.endpoint_id == id;
          })) { endpoint.endpoint_id = std::move(id); break; }
    }
    endpoints_.push_back({std::move(endpoint), "0", "2"});
    dirty_ = true;
    return true;
  }

  bool remove(std::size_t index) {
    const auto item = at(index);
    if (!editable() || !item || item->endpoint.backend != control::AudioRuntimeEndpointBackend::Wasapi)
      return false;
    endpoints_.erase(endpoints_.begin() + index);
    dirty_ = true;
    return true;
  }

  bool canMakeMaster(std::size_t index) const {
    if (!editable() || index >= endpoints_.size() ||
        endpoints_[index].endpoint.direction != control::AudioRuntimeEndpointDirection::Render)
      return false;
    // A physical ASIO render must remain the matrix master in the current engine.
    return !std::any_of(endpoints_.begin(), endpoints_.end(), [](const auto& candidate) {
          return candidate.endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio &&
                 candidate.endpoint.direction == control::AudioRuntimeEndpointDirection::Render;
        });
  }

  bool makeMaster(std::size_t index) {
    if (!canMakeMaster(index)) return false;
    for (std::size_t i = 0; i < endpoints_.size(); ++i)
      endpoints_[i].endpoint.clock_master = i == index;
    dirty_ = true;
    return true;
  }

  static bool matches(const platform::AudioDeviceDescriptor& device,
                      control::AudioRuntimeEndpointDirection direction) {
    if (device.backend != platform::AudioBackendKind::Wasapi &&
        device.backend != platform::AudioBackendKind::VirtualWasapi) return false;
    return device.direction == platform::AudioDeviceDirection::Duplex ||
      (direction == control::AudioRuntimeEndpointDirection::Capture
        ? device.direction == platform::AudioDeviceDirection::Input
        : device.direction == platform::AudioDeviceDirection::Output);
  }

  static bool matchesEndpoint(const platform::AudioDeviceDescriptor& device,
                             const control::AudioRuntimeEndpointConfiguration& endpoint) {
    if (endpoint.backend == control::AudioRuntimeEndpointBackend::Wasapi)
      return matches(device, endpoint.direction);
    return device.backend == platform::AudioBackendKind::Asio &&
        (device.direction == platform::AudioDeviceDirection::Duplex ||
         (endpoint.direction == control::AudioRuntimeEndpointDirection::Capture
            ? device.direction == platform::AudioDeviceDirection::Input
            : device.direction == platform::AudioDeviceDirection::Output));
  }

  std::optional<control::AudioRuntimeConfiguration> build(
      const std::vector<platform::AudioDeviceDescriptor>& devices, std::string& error) const {
    error.clear();
    if (!editable()) { error = "This runtime mode is preserved; create a new WASAPI matrix to replace it"; return {}; }
    auto configuration = source_;
    configuration.mode = control::AudioRuntimeMode::WasapiMatrix;
    configuration.endpoints.clear();
    for (const auto& draft : endpoints_) {
      auto endpoint = draft.endpoint;
      {
        if (!number(draft.firstChannel, 0, 255, endpoint.first_channel) ||
            !number(draft.channelCount, 1, 256, endpoint.channel_count)) {
          error = "Channel offset must be 0-255; channel count must be 1-256"; return {};
        }
        if (endpoint.endpoint_id.size() > 128 || endpoint.endpoint_id.empty() ||
            std::any_of(endpoint.endpoint_id.begin(), endpoint.endpoint_id.end(),
              [](unsigned char c) { return c < 0x20 || c == 0x7f; })) {
          error = "Endpoint ID must contain 1-128 bytes without control characters"; return {};
        }
        const auto device = std::find_if(devices.begin(), devices.end(), [&](const auto& candidate) {
          return candidate.id == endpoint.device_id && matchesEndpoint(candidate, endpoint);
        });
        if (device == devices.end()) { error = "Select an available device for " + endpoint.endpoint_id; return {}; }
        auto channels = endpoint.direction == control::AudioRuntimeEndpointDirection::Capture
            ? device->input_channels : device->output_channels;
        if (channels == 0 && endpoint.backend == control::AudioRuntimeEndpointBackend::Wasapi)
          for (const auto& format : device->formats) channels = (std::max)(channels, format.channels);
        if (channels != 0 && (endpoint.first_channel >= channels ||
            endpoint.channel_count > channels - endpoint.first_channel)) {
          error = "Channel range exceeds the reported device capability for " + endpoint.endpoint_id; return {};
        }
        if (endpoint.first_channel + endpoint.channel_count > 256) {
          error = "Channel range must fit within 256 native channels"; return {};
        }
        if (endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio &&
            (!number(draft.sampleRate, 8000, 768000, endpoint.sample_rate) ||
             !number(draft.blockFrames, 1, 65536, endpoint.block_frames))) {
          error = "ASIO sample rate must be 8000-768000 Hz; buffer must be 1-65536 frames";
          return {};
        }
      }
      configuration.endpoints.push_back(std::move(endpoint));
    }
    const auto errors = control::validate_audio_runtime_configuration(configuration, false);
    if (!errors.empty()) { error = errors.front().message; return {}; }
    return configuration;
  }

 private:
  static bool number(const std::string& text, std::uint32_t minimum,
                     std::uint32_t maximum, std::uint32_t& value) {
    if (text.empty() || text.size() > 10) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() &&
           value >= minimum && value <= maximum;
  }
  control::AudioRuntimeConfiguration source_;
  std::vector<RuntimeEndpointDraft> endpoints_;
  bool dirty_ = false;
};

}  // namespace sar::gui_vstgui
