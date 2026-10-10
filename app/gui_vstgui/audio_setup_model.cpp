#include "app/gui_vstgui/audio_setup_model.h"

#include <algorithm>
#include <utility>

namespace sar::gui_vstgui {

void AudioSetupModel::set_devices(
    std::vector<platform::AudioDeviceDescriptor> devices) {
  devices_ = std::move(devices);
}

void AudioSetupModel::load(
    const control::AudioRuntimeConfiguration& configuration) {
  endpoints_.clear();
  next_id_ = 1;
  if (configuration.mode == control::AudioRuntimeMode::PhysicalAsio) {
    return;
  }

  const auto restore_endpoint = [&](
      control::AudioRuntimeEndpointDirection direction,
      const std::string& endpoint_id, const std::string& device_id,
      std::uint32_t channel_count) {
    if (endpoint_id.empty() || device_id.empty() ||
        endpoints_.size() >= control::kMaximumAudioRuntimeEndpoints ||
        endpoint_id_in_use(endpoint_id)) {
      return;
    }
    const auto* device = device_for(AudioSetupEndpoint{
        .device_id = device_id,
        .direction = direction,
    });
    if (channel_count == 0 && device != nullptr) {
      channel_count = channels_for(*device, direction);
    }
    endpoints_.push_back({endpoint_id, device_id, direction, channel_count});
  };

  if (configuration.mode == control::AudioRuntimeMode::WasapiMatrix) {
    for (const auto& endpoint : configuration.endpoints) {
      if (endpoint.backend != control::AudioRuntimeEndpointBackend::Wasapi) {
        continue;
      }
      restore_endpoint(endpoint.direction, endpoint.endpoint_id,
                       endpoint.device_id, endpoint.channel_count);
    }
  } else if (configuration.mode == control::AudioRuntimeMode::WasapiDuplex) {
    if (!configuration.capture_device_id.empty()) {
      restore_endpoint(control::AudioRuntimeEndpointDirection::Capture,
                       "wasapi.capture", configuration.capture_device_id, 0);
    }
    if (!configuration.render_device_id.empty()) {
      restore_endpoint(control::AudioRuntimeEndpointDirection::Render,
                       "wasapi.render", configuration.render_device_id, 0);
    }
  } else if (configuration.mode == control::AudioRuntimeMode::WasapiRender &&
             !configuration.render_device_id.empty()) {
    restore_endpoint(control::AudioRuntimeEndpointDirection::Render,
                     "wasapi.render", configuration.render_device_id, 0);
  }

  const bool has_saved_configuration =
      !configuration.capture_device_id.empty() ||
      !configuration.render_device_id.empty() ||
      !configuration.endpoints.empty();
  if (endpoints_.empty() && !has_saved_configuration) {
    const auto* preferred = first_available(
        control::AudioRuntimeEndpointDirection::Render);
    if (preferred != nullptr) {
      static_cast<void>(add_render());
    }
  }
}

bool AudioSetupModel::add_capture() {
  const auto* device = first_available(
      control::AudioRuntimeEndpointDirection::Capture);
  if (device == nullptr) {
    return false;
  }
  const auto id = next_endpoint_id(control::AudioRuntimeEndpointDirection::Capture);
  return add(control::AudioRuntimeEndpointDirection::Capture, id, device->id,
             channels_for(*device,
                          control::AudioRuntimeEndpointDirection::Capture));
}

bool AudioSetupModel::add_render() {
  const auto* device = first_available(
      control::AudioRuntimeEndpointDirection::Render);
  if (device == nullptr) {
    return false;
  }
  const auto id = next_endpoint_id(control::AudioRuntimeEndpointDirection::Render);
  return add(control::AudioRuntimeEndpointDirection::Render, id, device->id,
             channels_for(*device,
                          control::AudioRuntimeEndpointDirection::Render));
}

bool AudioSetupModel::cycle_device(std::size_t endpoint_index, bool forward) {
  if (endpoint_index >= endpoints_.size()) {
    return false;
  }
  const auto& endpoint = endpoints_[endpoint_index];
  const auto current = std::ranges::find_if(
      devices_, [&](const auto& device) { return device.id == endpoint.device_id; });
  const auto current_index = current == devices_.end()
                                 ? devices_.size()
                                 : static_cast<std::size_t>(current - devices_.begin());
  const auto start = current_index == devices_.size()
                         ? (forward ? std::size_t{0} : devices_.size() - 1)
                         : (forward ? (current_index + 1) % devices_.size()
                                    : (current_index + devices_.size() - 1) %
                                          devices_.size());
  for (std::size_t offset = 0; offset < devices_.size(); ++offset) {
    const auto index = forward
                           ? (start + offset) % devices_.size()
                           : (start + devices_.size() - offset) % devices_.size();
    const auto& candidate = devices_[index];
    if (candidate.id == endpoint.device_id ||
        !supports(candidate, endpoint.direction) ||
        in_use(candidate.id, endpoint.direction, endpoint_index)) {
      continue;
    }
    auto& selected = endpoints_[endpoint_index];
    selected.device_id = candidate.id;
    selected.channel_count = channels_for(candidate, selected.direction);
    return true;
  }
  return false;
}

bool AudioSetupModel::remove(std::size_t endpoint_index) {
  if (endpoint_index >= endpoints_.size()) {
    return false;
  }
  endpoints_.erase(endpoints_.begin() +
                   static_cast<std::ptrdiff_t>(endpoint_index));
  return true;
}

const std::vector<AudioSetupEndpoint>& AudioSetupModel::endpoints() const noexcept {
  return endpoints_;
}

const std::vector<platform::AudioDeviceDescriptor>& AudioSetupModel::devices()
    const noexcept {
  return devices_;
}

const platform::AudioDeviceDescriptor* AudioSetupModel::device_for(
    const AudioSetupEndpoint& endpoint) const noexcept {
  const auto found = std::ranges::find_if(
      devices_, [&](const auto& device) { return device.id == endpoint.device_id; });
  return found == devices_.end() ? nullptr : &*found;
}

bool AudioSetupModel::can_add_capture() const noexcept {
  return endpoints_.size() < control::kMaximumAudioRuntimeEndpoints &&
         first_available(control::AudioRuntimeEndpointDirection::Capture) !=
             nullptr;
}

bool AudioSetupModel::can_add_render() const noexcept {
  return endpoints_.size() < control::kMaximumAudioRuntimeEndpoints &&
         first_available(control::AudioRuntimeEndpointDirection::Render) !=
             nullptr;
}

bool AudioSetupModel::can_apply() const noexcept {
  if (endpoints_.empty()) {
    return false;
  }
  std::size_t render_count = 0;
  bool master_assigned = false;
  for (std::size_t index = 0; index < endpoints_.size(); ++index) {
    const auto& endpoint = endpoints_[index];
    const auto* device = device_for(endpoint);
    if (device == nullptr || endpoint.endpoint_id.empty() ||
        endpoint.channel_count == 0 ||
        !supports(*device, endpoint.direction)) {
      return false;
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      const auto& other = endpoints_[previous];
      if (other.endpoint_id == endpoint.endpoint_id ||
          (other.device_id == endpoint.device_id &&
           other.direction == endpoint.direction)) {
        return false;
      }
    }
    if (endpoint.direction == control::AudioRuntimeEndpointDirection::Render) {
      ++render_count;
      master_assigned = true;
    }
  }
  return render_count > 0 && master_assigned;
}

control::AudioRuntimeConfiguration AudioSetupModel::configuration() const {
  control::AudioRuntimeConfiguration result;
  if (!can_apply()) {
    return result;
  }
  result.mode = control::AudioRuntimeMode::WasapiMatrix;
  bool clock_master_assigned = false;
  result.endpoints.reserve(endpoints_.size());
  for (const auto& endpoint : endpoints_) {
    const bool is_master =
        endpoint.direction == control::AudioRuntimeEndpointDirection::Render &&
        !clock_master_assigned;
    clock_master_assigned = clock_master_assigned || is_master;
    result.endpoints.push_back({
        endpoint.endpoint_id,
        endpoint.device_id,
        endpoint.direction,
        is_master,
        0,
        endpoint.channel_count,
    });
  }
  return result;
}

bool AudioSetupModel::add(
    control::AudioRuntimeEndpointDirection direction,
    std::string endpoint_id,
    std::string device_id,
    std::uint32_t channel_count) {
  if (endpoints_.size() >= control::kMaximumAudioRuntimeEndpoints) {
    return false;
  }
  const auto* device = device_for(AudioSetupEndpoint{
      .device_id = device_id,
      .direction = direction,
  });
  if (device == nullptr || !supports(*device, direction) ||
      in_use(device_id, direction, endpoints_.size()) || endpoint_id.empty()) {
    return false;
  }
  if (channel_count == 0) {
    channel_count = channels_for(*device, direction);
  }
  if (channel_count == 0) {
    return false;
  }
  endpoints_.push_back({std::move(endpoint_id), std::move(device_id), direction,
                        channel_count});
  return true;
}

bool AudioSetupModel::supports(
    const platform::AudioDeviceDescriptor& device,
    control::AudioRuntimeEndpointDirection direction) const noexcept {
  if (device.backend != platform::AudioBackendKind::Wasapi &&
      device.backend != platform::AudioBackendKind::VirtualWasapi) {
    return false;
  }
  if (direction == control::AudioRuntimeEndpointDirection::Capture) {
    return device.direction == platform::AudioDeviceDirection::Input ||
           device.direction == platform::AudioDeviceDirection::Duplex;
  }
  return device.direction == platform::AudioDeviceDirection::Output ||
         device.direction == platform::AudioDeviceDirection::Duplex;
}

std::uint32_t AudioSetupModel::channels_for(
    const platform::AudioDeviceDescriptor& device,
    control::AudioRuntimeEndpointDirection direction) const noexcept {
  const auto channels = direction == control::AudioRuntimeEndpointDirection::Capture
                            ? device.input_channels
                            : device.output_channels;
  if (channels > 0) {
    return channels;
  }
  return device.formats.empty() ? 0 : device.formats.front().channels;
}

const platform::AudioDeviceDescriptor* AudioSetupModel::first_available(
    control::AudioRuntimeEndpointDirection direction,
    std::size_t after_index) const noexcept {
  if (devices_.empty()) {
    return nullptr;
  }
  for (std::size_t offset = 0; offset < devices_.size(); ++offset) {
    const auto index = (after_index + offset) % devices_.size();
    const auto& device = devices_[index];
    if (supports(device, direction) &&
        !in_use(device.id, direction, endpoints_.size())) {
      return &device;
    }
  }
  return nullptr;
}

bool AudioSetupModel::in_use(
    const std::string& device_id,
    control::AudioRuntimeEndpointDirection direction,
    std::size_t except_index) const noexcept {
  for (std::size_t index = 0; index < endpoints_.size(); ++index) {
    if (index == except_index) {
      continue;
    }
    const auto& endpoint = endpoints_[index];
    if (endpoint.device_id == device_id && endpoint.direction == direction) {
      return true;
    }
  }
  return false;
}

std::string AudioSetupModel::next_endpoint_id(
    control::AudioRuntimeEndpointDirection direction) {
  const auto prefix = direction == control::AudioRuntimeEndpointDirection::Capture
                          ? "wasapi-capture-"
                          : "wasapi-render-";
  std::string candidate;
  do {
    candidate = prefix + std::to_string(next_id_++);
  } while (endpoint_id_in_use(candidate));
  return candidate;
}

bool AudioSetupModel::endpoint_id_in_use(std::string_view endpoint_id) const noexcept {
  return std::ranges::any_of(endpoints_, [&](const auto& endpoint) {
    return endpoint.endpoint_id == endpoint_id;
  });
}

}  // namespace sar::gui_vstgui
