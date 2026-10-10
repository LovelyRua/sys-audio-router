#include "core/service/audio_runtime_topology.h"

#include <cassert>

int main() {
  using sar::control::AudioRuntimeEndpointDirection;
  using sar::control::AudioRuntimeMode;

  sar::control::AudioRuntimeConfiguration legacy;
  legacy.mode = AudioRuntimeMode::WasapiDuplex;
  legacy.capture_device_id = "capture-native";
  legacy.render_device_id = "render-native";
  auto result = sar::service::build_audio_runtime_topology(legacy);
  assert(result.ok());
  assert(result.topology().endpoints.size() == 2);
  assert(result.topology().endpoints[0].endpoint_id == "wasapi.capture");
  assert(result.topology().endpoints[1].endpoint_id == "wasapi.render");
  assert(result.topology().clock_master_index == 1);

  sar::control::AudioRuntimeConfiguration matrix;
  matrix.mode = AudioRuntimeMode::WasapiMatrix;
  matrix.endpoints = {
      {"capture-a", "capture-native-a",
       AudioRuntimeEndpointDirection::Capture, false, 4, 2},
      {"render-a", "render-native-a", AudioRuntimeEndpointDirection::Render,
       false, 8, 2},
      {"render-main", "render-native-main",
       AudioRuntimeEndpointDirection::Render, true, 0, 2},
  };
  result = sar::service::build_audio_runtime_topology(matrix);
  assert(result.ok());
  assert(result.topology().endpoints.size() == 3);
  assert(result.topology().clock_master_index == 2);
  assert(result.topology().endpoints[2].clock_master);
  assert(result.topology().ports.size() == 6);
  assert(result.topology().ports[0].port_id == "capture-a.ch1");
  assert(result.topology().ports[3].port_id == "render-a.ch2");
  assert(result.topology().ports[0].native_channel == 4);
  assert(result.topology().ports[1].native_channel == 5);
  assert(result.topology().ports[2].native_channel == 8);
  assert(result.topology().ports[3].native_channel == 9);
  assert(result.topology().ports[4].native_channel == 0);
  assert(result.topology().ports[5].native_channel == 1);
  assert(result.topology().ports[4].clock_master);

  matrix.endpoints[2].clock_master = false;
  result = sar::service::build_audio_runtime_topology(matrix);
  assert(!result.ok());
  assert(!result.errors().empty());

  return 0;
}
