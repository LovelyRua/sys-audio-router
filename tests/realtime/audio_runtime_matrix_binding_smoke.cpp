#include "core/service/audio_runtime_matrix_binding.h"

#include <cassert>

int main() {
  using sar::control::AudioRuntimeEndpointDirection;
  sar::service::AudioRuntimeTopology topology;
  topology.endpoints = {
      {"capture-a", "capture-native", AudioRuntimeEndpointDirection::Capture,
       false, 0, 2},
      {"render-main", "render-native",
       AudioRuntimeEndpointDirection::Render, true, 0, 2},
      {"render-b", "render-native-b", AudioRuntimeEndpointDirection::Render,
       false, 0, 2},
  };

  sar::control::PresetRouteMatrix matrix;
  matrix.inputs = {{"asio.ch1", "ASIO 1"},
                   {"asio.ch2", "ASIO 2"},
                   {"capture-a.ch1", "Capture A 1"},
                   {"capture-a.ch2", "Capture A 2"}};
  matrix.outputs = {{"asio.ch1", "ASIO 1"},
                    {"asio.ch2", "ASIO 2"},
                    {"render-main.ch1", "Main 1"},
                    {"render-main.ch2", "Main 2"},
                    {"render-b.ch1", "B 1"},
                    {"render-b.ch2", "B 2"}};

  auto result = sar::service::bind_audio_runtime_to_matrix(topology, matrix);
  assert(result.ok());
  assert(result.bindings().size() == 3);
  assert(result.bindings()[0].graph_first_channel == 2);
  assert(result.bindings()[1].graph_first_channel == 2);
  assert(result.bindings()[2].graph_first_channel == 4);
  assert(result.bindings()[1].clock_master);

  std::swap(matrix.outputs[4], matrix.outputs[5]);
  result = sar::service::bind_audio_runtime_to_matrix(topology, matrix);
  assert(!result.ok());
  assert(result.errors()[0].code ==
         "audio_runtime_matrix_port_range_not_contiguous");

  matrix.outputs.erase(matrix.outputs.begin() + 4);
  result = sar::service::bind_audio_runtime_to_matrix(topology, matrix);
  assert(!result.ok());
  bool found_missing = false;
  for (const auto& error : result.errors()) {
    found_missing = found_missing ||
                    error.code == "audio_runtime_matrix_port_missing";
  }
  assert(found_missing);
  return 0;
}
