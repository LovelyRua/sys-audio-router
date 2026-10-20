#include "app/gui_vstgui/runtime_device_model.h"
#include "app/gui_vstgui/command_ids.h"
#include "core/control/control_wire_protocol.h"

#include <cstdlib>
#include <iostream>
#include <thread>
#include <unordered_set>
#include <mutex>

namespace {
using namespace sar;
using namespace gui_vstgui;
void require(bool condition, const char* message) {
  if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
platform::AudioDeviceDescriptor device(const char* id, platform::AudioDeviceDirection direction) {
  platform::AudioDeviceDescriptor value;
  value.id = id; value.label = id;
  value.backend = platform::AudioBackendKind::Wasapi;
  value.direction = direction;
  value.input_channels = direction == platform::AudioDeviceDirection::Input ? 2 : 0;
  value.output_channels = direction == platform::AudioDeviceDirection::Output ? 2 : 0;
  return value;
}
}

int main() {
  std::unordered_set<std::string> ids;
  std::mutex idsMutex;
  std::vector<std::thread> workers;
  for (int thread = 0; thread < 8; ++thread) {
    workers.emplace_back([&] {
      for (int window = 0; window < 32; ++window) {
        GuiCommandIds client;
        for (int request = 0; request < 16; ++request) {
          auto id = client.next();
          const std::lock_guard lock(idsMutex);
          require(ids.insert(std::move(id)).second, "Command ID collision across native windows");
        }
      }
    });
  }
  for (auto& worker : workers) worker.join();
  require(ids.size() == 4096, "Concurrent command sequence lost requests");
  using control::AudioRuntimeEndpointDirection;
  std::vector<platform::AudioDeviceDescriptor> devices{
      device("speaker", platform::AudioDeviceDirection::Output),
      device("mic", platform::AudioDeviceDirection::Input)};
  RuntimeDeviceDraft draft;
  std::string error;
  draft.load({});
  require(draft.editable() && !draft.dirty(), "Fresh draft state is wrong");
  require(!draft.build(devices, error) && !error.empty(), "Empty matrix accepted");
  require(draft.add(AudioRuntimeEndpointDirection::Render), "Could not add playback");
  require(draft.at(0)->endpoint.endpoint_id == "render-1" && draft.at(0)->endpoint.clock_master,
          "First playback must have a unique ID and default clock");
  require(!draft.build(devices, error), "Missing device binding accepted");
  draft.at(0)->endpoint.device_id = "speaker";
  auto configuration = draft.build(devices, error);
  require(configuration.has_value() && error.empty(), "Valid playback rejected");
  require(configuration->mode == control::AudioRuntimeMode::WasapiMatrix, "Wrong runtime mode");
  control::ControlCommand command;
  command.command_id = "gui-runtime-test";
  command.type = control::ControlCommandType::ConfigureAudioRuntime;
  command.audio_runtime = *configuration;
  const auto encoded = control::encode_control_command(command);
  require(encoded.ok(), "Draft cannot use the existing wire protocol");
  const auto decoded = control::decode_control_command(encoded.bytes);
  require(decoded.ok() && decoded.command.audio_runtime.endpoints.size() == 1,
          "Wire round trip lost endpoint configuration");
  require(draft.add(AudioRuntimeEndpointDirection::Capture), "Could not add capture");
  draft.at(1)->endpoint.device_id = "mic";
  require(!draft.makeMaster(1), "Capture became master");
  configuration = draft.build(devices, error);
  require(configuration && configuration->endpoints.size() == 2, "Duplex draft rejected");
  draft.at(1)->endpoint.device_id = "speaker";
  require(!draft.build(devices, error), "Direction-incompatible device accepted");
  draft.at(1)->endpoint.device_id = "mic";
  auto wrongBackend = devices[1]; wrongBackend.backend = platform::AudioBackendKind::Asio;
  require(!RuntimeDeviceDraft::matches(wrongBackend, AudioRuntimeEndpointDirection::Capture),
          "Physical ASIO leaked into WASAPI dropdown");
  for (const auto* invalid : {"", "-1", "2x", "+2", " 2", "0", "257", "4294967296"}) {
    draft.at(1)->channelCount = invalid;
    require(!draft.build(devices, error), "Invalid channel count accepted");
  }
  draft.at(1)->channelCount = "2";
  draft.at(1)->firstChannel = "1";
  require(!draft.build(devices, error), "Out-of-range channel slice accepted");
  draft.at(1)->firstChannel = "0";
  draft.at(1)->endpoint.endpoint_id = "render-1";
  require(!draft.build(devices, error), "Duplicate stable ID accepted");
  draft.at(1)->endpoint.endpoint_id = "capture-1";
  draft.at(1)->endpoint.device_id = "disappeared";
  require(!draft.build(devices, error), "Unavailable device silently rebound");
  draft.at(1)->endpoint.device_id = "mic";
  require(draft.build(devices, error).has_value(), "Valid draft did not recover");
  require(draft.add(AudioRuntimeEndpointDirection::Render), "Second render unavailable");
  require(draft.at(2)->endpoint.endpoint_id == "render-2", "Generated duplicate ID");
  draft.at(2)->endpoint.device_id = "speaker";
  require(!draft.build(devices, error), "Same native direction bound twice");
  require(draft.makeMaster(2) && !draft.at(0)->endpoint.clock_master, "Clock selection not exclusive");
  require(draft.remove(2), "Endpoint removal failed");
  require(!draft.build(devices, error), "Removing master silently selected another clock");
  require(draft.makeMaster(0), "Could not explicitly restore master");
  configuration = draft.build(devices, error);
  require(configuration.has_value(), "Repaired draft rejected");
  draft.load(*configuration);
  require(!draft.dirty(), "Successful readback stayed dirty");
  require(draft.at(1)->endpoint.device_id == "mic", "Readback binding changed");
  control::AudioRuntimeConfiguration legacy;
  legacy.mode = control::AudioRuntimeMode::PhysicalAsio;
  legacy.physical_asio_driver_clsid = "preserved-driver";
  draft.load(legacy);
  require(!draft.editable() && !draft.add(AudioRuntimeEndpointDirection::Render),
          "Unsupported runtime was silently editable");
  require(!draft.build(devices, error), "Unsupported runtime was silently converted");
  draft.newMatrix();
  require(draft.editable() && draft.dirty() && draft.endpoints().empty(), "Explicit reset failed");
  for (std::size_t i = 0; i < control::kMaximumAudioRuntimeEndpoints; ++i)
    require(draft.add(AudioRuntimeEndpointDirection::Render), "Capacity below engine limit");
  require(!draft.add(AudioRuntimeEndpointDirection::Render), "Endpoint capacity exceeded");
  require(!draft.remove(1000) && !draft.makeMaster(1000), "Invalid index accepted");

  control::AudioRuntimeConfiguration mixed;
  mixed.mode = control::AudioRuntimeMode::WasapiMatrix;
  control::AudioRuntimeEndpointConfiguration physical;
  physical.backend = control::AudioRuntimeEndpointBackend::PhysicalAsio;
  physical.endpoint_id = "asio-in"; physical.device_id = "driver";
  physical.device_group_id = "group"; physical.channel_count = 2;
  physical.sample_rate = 48000; physical.block_frames = 128;
  mixed.endpoints.push_back(physical);
  physical.endpoint_id = "asio-out";
  physical.direction = AudioRuntimeEndpointDirection::Render; physical.clock_master = true;
  mixed.endpoints.push_back(physical);
  draft.load(mixed);
  require(!draft.remove(0), "Native WASAPI editor removed physical ASIO group member");
  require(draft.add(AudioRuntimeEndpointDirection::Render), "Mixed WASAPI addition rejected");
  draft.at(2)->endpoint.device_id = "speaker";
  require(!draft.canMakeMaster(2) && !draft.makeMaster(2), "ASIO master constraint ignored");
  const auto preserved = draft.build(devices, error);
  require(preserved && preserved->endpoints[0].device_group_id == "group" &&
      preserved->endpoints[1].clock_master && preserved->endpoints[0].sample_rate == 48000,
      "Physical ASIO fields lost while adding WASAPI follower");
  std::cout << "Native runtime device draft checks passed\n";
}
