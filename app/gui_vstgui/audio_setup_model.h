#pragma once

#include "core/control/control_command.h"
#include "core/platform/audio_device.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sar::gui_vstgui {

struct AudioSetupEndpoint {
  std::string endpoint_id;
  std::string device_id;
  control::AudioRuntimeEndpointDirection direction =
      control::AudioRuntimeEndpointDirection::Capture;
  std::uint32_t channel_count = 0;
};

// Owns the editable WASAPI endpoint selection used by the setup page. It has
// no VSTGUI dependency so selection and validation remain deterministic.
class AudioSetupModel final {
 public:
  void set_devices(std::vector<platform::AudioDeviceDescriptor> devices);
  void load(const control::AudioRuntimeConfiguration& configuration);

  [[nodiscard]] bool add_capture();
  [[nodiscard]] bool add_render();
  [[nodiscard]] bool cycle_device(std::size_t endpoint_index,
                                  bool forward = true);
  [[nodiscard]] bool remove(std::size_t endpoint_index);

  [[nodiscard]] const std::vector<AudioSetupEndpoint>& endpoints() const noexcept;
  [[nodiscard]] const std::vector<platform::AudioDeviceDescriptor>& devices()
      const noexcept;
  [[nodiscard]] const platform::AudioDeviceDescriptor* device_for(
      const AudioSetupEndpoint& endpoint) const noexcept;
  [[nodiscard]] bool can_add_capture() const noexcept;
  [[nodiscard]] bool can_add_render() const noexcept;
  [[nodiscard]] bool can_apply() const noexcept;
  [[nodiscard]] control::AudioRuntimeConfiguration configuration() const;

 private:
  [[nodiscard]] bool add(control::AudioRuntimeEndpointDirection direction,
                         std::string endpoint_id,
                         std::string device_id,
                         std::uint32_t channel_count);
  [[nodiscard]] bool supports(
      const platform::AudioDeviceDescriptor& device,
      control::AudioRuntimeEndpointDirection direction) const noexcept;
  [[nodiscard]] std::uint32_t channels_for(
      const platform::AudioDeviceDescriptor& device,
      control::AudioRuntimeEndpointDirection direction) const noexcept;
  [[nodiscard]] const platform::AudioDeviceDescriptor* first_available(
      control::AudioRuntimeEndpointDirection direction,
      std::size_t after_index = 0) const noexcept;
  [[nodiscard]] bool in_use(
      const std::string& device_id,
      control::AudioRuntimeEndpointDirection direction,
      std::size_t except_index) const noexcept;
  [[nodiscard]] std::string next_endpoint_id(
      control::AudioRuntimeEndpointDirection direction);
  [[nodiscard]] bool endpoint_id_in_use(std::string_view endpoint_id) const noexcept;

  std::vector<platform::AudioDeviceDescriptor> devices_;
  std::vector<AudioSetupEndpoint> endpoints_;
  std::size_t next_id_ = 1;
};

}  // namespace sar::gui_vstgui
