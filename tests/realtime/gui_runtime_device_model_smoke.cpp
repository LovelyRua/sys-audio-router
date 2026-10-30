#include "app/gui_vstgui/runtime_device_model.h"
#include "app/gui_vstgui/command_ids.h"
#include "app/gui_vstgui/request_error_state.h"
#include "app/gui_vstgui/matrix_page.h"
#include "app/gui_vstgui/route_gain_text.h"
#include "app/gui_vstgui/route_edit_queue.h"
#include "app/gui_vstgui/asio_device_model.h"
#include <limits>
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
  AsioTopologyDraft asio;
  const auto defaultAsio = control::default_virtual_asio_device_definition();
  asio.load({defaultAsio});
  std::string asioError;
  require(!asio.dirty() && asio.build(asioError).has_value(), "Valid ASIO topology rejected");
  require(!asio.remove(0), "Last ASIO device removed");
  require(asio.add("7350AB15-3850-452D-8E87-CFC14A95A101"), "ASIO device addition failed");
  const auto addedIdentity = asio.at(1)->device;
  require(!asio.add("7350AB15-3850-452D-8E87-CFC14A95A101"), "Duplicate ASIO identity accepted");
  require(!asio.add("not-a-guid"), "Malformed ASIO identity accepted");
  for (const auto* invalid : {"", "0", "65", "-1", "+2", " 2", "2x", "4294967296"}) {
    asio.at(1)->inputs = invalid;
    require(!asio.build(asioError), "Invalid ASIO channel text accepted");
  }
  asio.at(1)->inputs = "4";
  require(!asio.build(asioError), "Unbalanced ASIO channel totals accepted");
  asio.at(1)->outputs = "4";
  auto validAsio = asio.build(asioError);
  require(validAsio && validAsio->at(1).input_channels == 4, "ASIO channel edits lost");
  require(validAsio->at(1).clsid == addedIdentity.clsid &&
      validAsio->at(1).broker_token == addedIdentity.broker_token, "Editing channels changed stable ASIO identity");
  asio.at(1)->device.registry_name = defaultAsio.registry_name;
  require(!asio.build(asioError), "Duplicate ASIO display name accepted");
  for (const auto* invalid : {"", " leading", "trailing ", "path/name", "path\\name", "line\nname"}) {
    asio.at(1)->device.registry_name = invalid;
    require(!asio.build(asioError), "Invalid ASIO device name accepted");
  }
  asio.at(1)->device.registry_name = "SAR Monitor";
  asio.at(0)->device.enabled = false; asio.at(1)->device.enabled = false;
  require(!asio.build(asioError), "Topology with no enabled ASIO channels accepted");
  asio.at(1)->device.enabled = true;
  require(asio.build(asioError).has_value(), "Disabled device was incorrectly counted in totals");
  require(asio.remove(0) && asio.dirty(), "ASIO device removal failed");
  asio.load({defaultAsio});
  require(!asio.dirty(), "ASIO saved readback remained dirty");
  for (unsigned index = 1; index < control::kMaximumVirtualAsioDevices; ++index) {
    const auto suffix = std::to_string(index);
    require(asio.add("7350AB15-3850-452D-8E87-" + std::string(12 - suffix.size(), '0') + suffix),
            "ASIO device limit is below sixteen");
  }
  require(!asio.add("7350AB15-3850-452D-8E87-000000001000"), "ASIO device limit exceeded");
  require(asio.build(asioError).has_value(), "Sixteen ASIO devices failed validation");
  control::ControlCommand reloadAsio;
  reloadAsio.command_id = "native-asio-reload-roundtrip";
  reloadAsio.type = control::ControlCommandType::ReloadVirtualAsioDevices;
  reloadAsio.virtual_asio_devices = *asio.build(asioError);
  auto reloadBytes = control::encode_control_command(reloadAsio);
  require(reloadBytes.ok(), "ASIO reload encoding failed");
  const auto reloadRoundtrip = control::decode_control_command(reloadBytes.bytes);
  require(reloadRoundtrip.ok() && reloadRoundtrip.command.type == reloadAsio.type &&
      reloadRoundtrip.command.virtual_asio_devices == reloadAsio.virtual_asio_devices,
      "ASIO reload wire round trip changed identities");
  reloadBytes.bytes[4] = 13; reloadBytes.bytes[5] = 0;
  require(!control::decode_control_command(reloadBytes.bytes).ok(),
          "ASIO reload command accepted on an unsupported wire version");
  control::PresetRouteMatrix authoritative;
  graph::RouteEndpointDescriptor source;
  source.id = "source";
  graph::RouteEndpointDescriptor destination;
  destination.id = "destination";
  authoritative.inputs.push_back(source);
  authoritative.outputs.push_back(destination);
  RouteEditQueue edits;
  for (const bool shift : {false, true}) {
    const auto create = routeClickCommand("source", "destination", nullptr, shift);
    require(create.action == RouteAction::Connect && create.gain == 1.0F,
            "Click on an absent route did not connect at unity");
  }
  control::PresetRoute quiet{"source", "destination", 0.125F, false};
  const auto mute = routeClickCommand("source", "destination", &quiet, false);
  require(mute.action == RouteAction::Mute && mute.mute && mute.gain == quiet.gain,
          "Ordinary click deleted the route or reset its gain");
  quiet.muted = true;
  const auto unmute = routeClickCommand("source", "destination", &quiet, false);
  require(unmute.action == RouteAction::Mute && !unmute.mute && unmute.gain == quiet.gain,
          "Unmute did not retain the configured gain");
  for (const bool muted : {false, true}) {
    quiet.muted = muted;
    require(routeClickCommand("source", "destination", &quiet, true).action == RouteAction::Disconnect,
            "Shift-click did not remove an existing route");
  }
  require(!edits.begin() && !edits.pending(), "Empty edit queue started a request");
  require(edits.enqueue({RouteAction::Connect, "source", "destination"}), "Connect rejected");
  auto shown = edits.project(authoritative);
  require(shown.routes.size() == 1 && authoritative.routes.empty(),
          "Pending route must be visible without mutating the received snapshot");
  const auto connect = edits.begin();
  require(connect && connect->action == RouteAction::Connect && !edits.begin(),
          "More than one route writer was dispatched");
  require(edits.enqueue({RouteAction::Gain, "source", "destination", 0.5F}), "Gain rejected");
  require(edits.enqueue({RouteAction::Gain, "source", "destination", 0.25F}), "Gain replacement rejected");
  shown = edits.project(authoritative);
  require(shown.routes.size() == 1 && shown.routes[0].gain == 0.25F,
          "A stale poll overwrote the latest fader intent");
  // The acknowledged connect snapshot arrives before the queued gain request.
  authoritative.routes.push_back({"source", "destination", 1.0F, false});
  edits.complete(true);
  require(edits.project(authoritative).routes[0].gain == 0.25F,
          "Acknowledgement briefly snapped the fader back");
  const auto gain = edits.begin();
  require(gain && gain->gain == 0.25F && !edits.queued(), "Fader events did not coalesce");
  require(edits.enqueue({RouteAction::Mute, "source", "destination", 0.0F, true}), "Mute rejected");
  require(edits.enqueue({RouteAction::Gain, "source", "destination", 0.1F}), "Gain after mute rejected");
  require(edits.project(authoritative).routes[0].muted, "Mute projection lost");
  edits.complete(true);
  require(edits.begin()->action == RouteAction::Mute, "Gain coalescing reordered mute");
  edits.complete(true);
  require(edits.begin()->action == RouteAction::Gain, "Ordered gain edit lost");
  edits.complete(false);
  require(!edits.pending() && edits.project(authoritative).routes[0].gain == 1.0F,
          "Rejected edit remained in the displayed matrix");
  require(edits.enqueue({RouteAction::Disconnect, "source", "destination"}), "Disconnect rejected");
  require(edits.enqueue({RouteAction::Connect, "source", "destination"}), "Rapid reconnect rejected");
  require(edits.project(authoritative).routes.size() == 1, "Rapid route toggles lost ordering");
  require(edits.begin()->action == RouteAction::Disconnect, "Toggle dispatch order changed");
  edits.complete(false);
  require(!edits.pending(), "Dependent edits survived a rejected disconnect");
  require(edits.enqueue({RouteAction::Gain, "source", "destination", 0.0F}), "Silence gain rejected");
  require(edits.project({}).routes.empty(), "Pending edit resurrected a removed endpoint");
  edits.clear();
  for (std::size_t index = 0; index < RouteEditQueue::kCapacity; ++index)
    require(edits.enqueue({RouteAction::Mute, "source", "destination", 0.0F, index % 2 == 0}),
            "Edit queue capacity below its documented bound");
  require(!edits.enqueue({RouteAction::Connect, "source", "destination"}), "Unbounded edit queue accepted");
  require(edits.begin().has_value(), "Full queue could not dispatch");
  require(!edits.enqueue({RouteAction::Connect, "source", "destination"}), "Active command escaped queue bound");
  edits.clear();
  require(!edits.pending(), "Disconnect did not discard pending edits");
  require(!edits.enqueue({RouteAction::Connect, "", "destination"}), "Empty source accepted");
  require(!edits.enqueue({RouteAction::Gain, "source", "destination", -1.0F}), "Negative gain queued");
  require(!edits.enqueue({RouteAction::Gain, "source", "destination", std::numeric_limits<float>::quiet_NaN()}),
          "NaN gain queued");
  require(routeGainText(0.0F) == "-inf dB", "Zero gain was not silence");
  require(routeGainText(1.0F) == "0.0 dB", "Unity gain display incorrect");
  require(routeGainText(0.001F) == "-60.0 dB", "Finite low gain displayed as silence");
  require(routeGainText(0.0001F) == "-80.0 dB", "Sub-fader gain lost its actual value");
  require(routeGainText(-1.0F) == "-- dB", "Invalid gain displayed as valid");
  require(routeGainText(std::numeric_limits<float>::infinity()) == "-- dB", "Infinite gain displayed as valid");
  require(routeGainText(std::numeric_limits<float>::quiet_NaN()) == "-- dB", "NaN gain displayed as valid");
  MatrixPage page(8);
  require(page.visibleCount() == 0 && !page.contains(0), "Empty page exposed a phantom slot");
  require(!page.next() && !page.previous(), "Empty page navigation accepted");
  page.reconcile(17);
  require(page.next() && page.next() && !page.next() && page.first() == 16,
          "Partial last page navigation failed");
  require(page.visibleCount() == 1 && page.contains(0) && !page.contains(1),
          "Partial page exposed nonexistent channels");
  page.reconcile(8);
  require(page.visibleCount() == 8 && page.contains(7) && !page.contains(8),
          "Full page exposed a slot beyond capacity");
  require(page.first() == 0 && !page.hasNext() && !page.hasPrevious(),
          "Shrinking topology stranded the matrix on an empty page");
  page.reconcile(16);
  require(page.next() && !page.next() && page.first() == 8,
          "Exact page boundary produced a phantom page");
  page.reconcile(9);
  require(page.first() == 8, "Valid selected page was unnecessarily reset");
  page.reconcile(0);
  require(page.first() == 0 && !page.next(), "Empty topology did not reset navigation");
  MatrixPage one(0);
  one.reconcile(2);
  require(one.next() && one.first() == 1 && !one.next(), "Zero capacity was unsafe");
  for (std::size_t count = 0; count < 257; ++count) {
    page.reconcile(count);
    while (page.previous()) {}
    while (page.next()) {}
    require(page.visibleCount() <= 8, "Visible row count exceeded page capacity");
    for (std::size_t slot = 0; slot <= 8; ++slot)
      require(page.contains(slot) == (slot < 8 && page.first() + slot < count),
              "Slot visibility diverged from the authoritative channel count");
    require(count == 0 ? page.first() == 0 : page.first() < count,
            "Page starts outside the authoritative topology");
    for (std::size_t reduced = count; reduced > 0; --reduced) {
      page.reconcile(reduced);
      require(page.first() < reduced, "Repeated topology shrink lost the active page");
    }
  }
  GuiRequestErrorState errors;
  errors.observe("offline", false);
  require(errors.message() == "offline", "Transport error was hidden");
  errors.observe("", true);
  require(errors.message().empty(), "Recovered transport stayed faulted");
  errors.observe("command rejected", true);
  errors.observe("", true);
  require(errors.message() == "command rejected", "Action error disappeared on routine poll");
  errors.clear();
  require(errors.message().empty(), "Explicit error clear failed");
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
