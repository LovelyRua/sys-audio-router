#pragma once

#include "core/control/session_document.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <vector>

namespace sar::gui_vstgui {
struct AsioDeviceDraft {
  control::VirtualAsioDeviceDefinition device;
  std::string inputs, outputs;
};

class AsioTopologyDraft {
 public:
  void load(const std::vector<control::VirtualAsioDeviceDefinition>& devices) {
    items_.clear();
    for (const auto& device : devices)
      items_.push_back({device, std::to_string(device.input_channels),
                       std::to_string(device.output_channels)});
    dirty_ = false;
  }
  const auto& items() const { return items_; }
  bool dirty() const { return dirty_; }
  void changed() { dirty_ = true; }
  AsioDeviceDraft* at(int index) {
    return index >= 0 && static_cast<std::size_t>(index) < items_.size() ? &items_[index] : nullptr;
  }
  bool add(const std::string& uuid) {
    if (items_.size() >= control::kMaximumVirtualAsioDevices) return false;
    control::VirtualAsioDeviceDefinition device;
    device.device_id = "asio-" + uuid;
    device.clsid = "{" + uuid + "}";
    device.broker_token = "virtual-asio-" + uuid;
    for (int n = 1; n <= 100; ++n) {
      device.registry_name = "System Audio Route " + std::to_string(n);
      if (std::none_of(items_.begin(), items_.end(), [&](const auto& item) {
            return item.device.registry_name == device.registry_name;
          })) break;
    }
    auto candidate = definitions();
    candidate.push_back(device);
    if (!control::validate_virtual_asio_devices(candidate).empty()) return false;
    items_.push_back({std::move(device), "2", "2"});
    dirty_ = true;
    return true;
  }
  bool remove(int index) {
    if (!at(index) || items_.size() <= 1) return false;
    items_.erase(items_.begin() + index);
    dirty_ = true;
    return true;
  }
  std::optional<std::vector<control::VirtualAsioDeviceDefinition>> build(std::string& error) const {
    error.clear();
    auto devices = definitions();
    std::uint64_t inputs = 0, outputs = 0;
    for (std::size_t i = 0; i < devices.size(); ++i) {
      auto& device = devices[i];
      if (!channelCount(items_[i].inputs, device.input_channels) ||
          !channelCount(items_[i].outputs, device.output_channels)) {
        error = "Channel counts must be whole numbers between 1 and 64.";
        return std::nullopt;
      }
      const auto& name = device.registry_name;
      if (name.empty() || name.size() > 128 || name.front() == ' ' || name.back() == ' ' ||
          std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; })) {
        error = "Device names must contain 1-128 bytes with no control characters or outer spaces.";
        return std::nullopt;
      }
      if (device.enabled) { inputs += device.input_channels; outputs += device.output_channels; }
    }
    const auto validation = control::validate_virtual_asio_devices(devices);
    if (!validation.empty()) { error = validation.front().message; return std::nullopt; }
    if (!inputs || inputs != outputs) {
      error = "Enabled input and output channel totals must be equal and nonzero.";
      return std::nullopt;
    }
    return devices;
  }
 private:
  static bool channelCount(const std::string& text, std::uint32_t& value) {
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && value > 0 && value <= 64;
  }
  std::vector<control::VirtualAsioDeviceDefinition> definitions() const {
    std::vector<control::VirtualAsioDeviceDefinition> devices;
    for (const auto& item : items_) devices.push_back(item.device);
    return devices;
  }
  bool dirty_ = false;
  std::vector<AsioDeviceDraft> items_;
};
}  // namespace sar::gui_vstgui
