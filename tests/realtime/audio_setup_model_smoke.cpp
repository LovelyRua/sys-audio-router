#include "app/gui_vstgui/audio_setup_model.h"

#include "core/control/control_command.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <utility>
#include <vector>

namespace {

using sar::control::AudioRuntimeEndpointDirection;
using sar::platform::AudioBackendKind;
using sar::platform::AudioDeviceDescriptor;
using sar::platform::AudioDeviceDirection;

AudioDeviceDescriptor device(std::string id, AudioBackendKind backend,
                             AudioDeviceDirection direction,
                             std::uint32_t inputs, std::uint32_t outputs,
                             bool is_default = false) {
  AudioDeviceDescriptor result;
  result.id = std::move(id);
  result.label = result.id;
  result.backend = backend;
  result.direction = direction;
  result.input_channels = inputs;
  result.output_channels = outputs;
  result.is_default = is_default;
  return result;
}

}  // namespace

int main() {
  using namespace sar;
  gui_vstgui::AudioSetupModel model;
  model.set_devices({
      device("render-default", AudioBackendKind::Wasapi,
             AudioDeviceDirection::Output, 0, 2, true),
      device("render-wide", AudioBackendKind::VirtualWasapi,
             AudioDeviceDirection::Output, 0, 8),
      device("capture-main", AudioBackendKind::Wasapi,
             AudioDeviceDirection::Input, 2, 0),
      device("capture-aux", AudioBackendKind::VirtualWasapi,
             AudioDeviceDirection::Input, 4, 0),
      device("loopback", AudioBackendKind::WasapiLoopback,
             AudioDeviceDirection::Input, 2, 0),
      device("asio", AudioBackendKind::Asio,
             AudioDeviceDirection::Duplex, 8, 8),
  });

  control::AudioRuntimeConfiguration empty;
  model.load(empty);
  assert(model.endpoints().size() == 1);
  assert(model.endpoints().front().device_id == "render-default");
  assert(model.available_channels(model.endpoints().front()) == 2);
  assert(model.can_add_render());
  assert(model.add_capture());
  assert(model.endpoints().size() == 2);
  assert(model.endpoints()[1].device_id == "capture-main");
  assert(model.add_capture());
  assert(model.endpoints()[2].device_id == "capture-aux");
  assert(!model.can_add_capture());
  assert(!model.add_capture());
  assert(model.cycle_device(0, true));
  assert(model.endpoints()[0].device_id == "render-wide");
  assert(model.endpoints()[0].first_channel == 0);
  assert(model.cycle_device(0, false));
  assert(model.endpoints()[0].device_id == "render-default");
  assert(model.add_render());
  assert(model.endpoints().back().device_id == "render-wide");
  assert(!model.can_add_render());
  assert(!model.cycle_device(0, true));

  const auto configuration = model.configuration();
  assert(configuration.mode == control::AudioRuntimeMode::WasapiMatrix);
  assert(configuration.endpoints.size() == 4);
  assert(std::count_if(configuration.endpoints.begin(), configuration.endpoints.end(),
                       [](const auto& endpoint) { return endpoint.clock_master; }) == 1);
  assert(configuration.endpoints.front().direction ==
         AudioRuntimeEndpointDirection::Render);
  assert(configuration.endpoints.front().channel_count == 2);
  auto restored_slice = configuration;
  restored_slice.endpoints[2].first_channel = 1;
  model.load(restored_slice);
  assert(model.endpoints()[2].first_channel == 1);
  assert(model.configuration().endpoints[2].first_channel == 1);
  model.load(configuration);
  assert(model.set_first_channel(2, 1));
  assert(model.set_channel_count(2, 2));
  assert(model.configuration().endpoints[2].first_channel == 1);
  assert(model.configuration().endpoints[2].channel_count == 2);
  assert(!model.set_first_channel(1, 1));
  assert(model.endpoints()[1].first_channel == 0);
  assert(!model.set_channel_count(1, 4));
  assert(model.endpoints()[1].channel_count == 2);
  assert(!model.set_channel_count(1, 0));
  assert(!model.set_first_channel(99, 0));
  assert(control::validate_audio_runtime_configuration(
             model.configuration(), false).empty());

  auto stable_ids = configuration;
  stable_ids.endpoints.resize(2);
  stable_ids.endpoints[1].endpoint_id = "wasapi-capture-1";
  model.load(stable_ids);
  assert(model.add_capture());
  const auto& generated = model.endpoints().back().endpoint_id;
  assert(generated == "wasapi-capture-2");
  assert(model.can_apply());

  auto disconnected_device = stable_ids;
  disconnected_device.endpoints.front().device_id = "temporarily-unplugged";
  model.load(disconnected_device);
  assert(model.endpoints().size() == 2);
  assert(model.device_for(model.endpoints().front()) == nullptr);
  assert(!model.can_apply());
  assert(model.cycle_device(0, true));
  assert(model.device_for(model.endpoints().front()) != nullptr);
  assert(model.can_apply());

  assert(model.remove(1));
  assert(model.can_apply());
  while (!model.endpoints().empty()) {
    assert(model.remove(model.endpoints().size() - 1));
  }
  assert(!model.can_apply());
  assert(!model.remove(0));

  control::AudioRuntimeConfiguration physical;
  physical.mode = control::AudioRuntimeMode::PhysicalAsio;
  model.load(physical);
  assert(model.endpoints().empty());
  assert(model.add_render());
  assert(model.can_apply());
}
