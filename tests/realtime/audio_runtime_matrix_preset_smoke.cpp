#include "core/service/audio_runtime_matrix_preset.h"

#include <cassert>

namespace {

sar::control::PresetDocument make_alpha_preset() {
  sar::control::PresetDocument preset;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs = {
      {"wasapi-capture-l", "WASAPI Capture L"},
      {"wasapi-capture-r", "WASAPI Capture R"},
      {"asio-output-l", "ASIO Output L"},
      {"asio-output-r", "ASIO Output R"},
  };
  preset.matrix.outputs = {
      {"wasapi-render-l", "WASAPI Render L"},
      {"wasapi-render-r", "WASAPI Render R"},
      {"asio-input-l", "ASIO Input L"},
      {"asio-input-r", "ASIO Input R"},
  };
  preset.matrix.routes = {
      {"asio-output-l", "wasapi-render-l", 1.0F, false},
      {"wasapi-capture-l", "asio-input-l", 1.0F, false},
      {"asio-output-r", "asio-input-r", 0.5F, false},
  };
  return preset;
}

}  // namespace

int main() {
  using Direction = sar::control::AudioRuntimeEndpointDirection;
  using Mode = sar::control::AudioRuntimeMode;

  sar::control::AudioRuntimeConfiguration first;
  first.mode = Mode::WasapiMatrix;
  first.endpoints = {
      {"capture-a", "capture-native-a", Direction::Capture, false, 0, 2},
      {"capture-b", "capture-native-b", Direction::Capture, false, 0, 2},
      {"render-main", "render-native-main", Direction::Render, true, 0, 2},
      {"render-b", "render-native-b", Direction::Render, false, 0, 2},
  };
  auto result = sar::service::reconcile_audio_runtime_matrix_preset(
      make_alpha_preset(), {}, first);
  assert(result.ok());
  assert(result.preset().matrix.inputs.size() == 6);
  assert(result.preset().matrix.outputs.size() == 6);
  assert(result.preset().matrix.inputs[0].id == "capture-a.ch1");
  assert(result.preset().matrix.inputs[2].id == "capture-b.ch1");
  assert(result.preset().matrix.inputs[4].id == "asio-output-l");
  assert(result.preset().matrix.outputs[0].id == "render-main.ch1");
  assert(result.preset().matrix.outputs[4].id == "asio-input-l");
  assert(result.preset().matrix.routes.size() == 1);
  assert(result.preset().matrix.routes[0].input_id == "asio-output-r");
  assert(result.preset().matrix.routes[0].output_id == "asio-input-r");

  sar::control::AudioRuntimeConfiguration second;
  second.mode = Mode::WasapiMatrix;
  second.endpoints = {
      {"capture-b", "capture-native-b", Direction::Capture, false, 0, 2},
      {"render-main", "render-native-main", Direction::Render, true, 0, 2},
  };
  auto reconfigured = sar::service::reconcile_audio_runtime_matrix_preset(
      result.preset(), first, second);
  assert(reconfigured.ok());
  assert(reconfigured.preset().matrix.inputs.size() == 4);
  assert(reconfigured.preset().matrix.outputs.size() == 4);
  assert(reconfigured.preset().matrix.inputs[0].id == "capture-b.ch1");
  assert(reconfigured.preset().matrix.inputs[2].id == "asio-output-l");
  assert(reconfigured.preset().matrix.routes.size() == 1);

  sar::control::AudioRuntimeConfiguration legacy;
  legacy.mode = Mode::WasapiDuplex;
  legacy.capture_device_id = "capture-native-a";
  legacy.render_device_id = "render-native-main";
  auto restored = sar::service::reconcile_audio_runtime_matrix_preset(
      result.preset(), first, legacy);
  assert(restored.ok());
  assert(restored.preset().matrix.inputs.size() == 4);
  assert(restored.preset().matrix.outputs.size() == 4);
  assert(restored.preset().matrix.inputs[0].id == "wasapi-capture-l");
  assert(restored.preset().matrix.inputs[1].id == "wasapi-capture-r");
  assert(restored.preset().matrix.inputs[2].id == "asio-output-l");
  assert(restored.preset().matrix.outputs[0].id == "wasapi-render-l");
  assert(restored.preset().matrix.outputs[2].id == "asio-input-l");
  assert(restored.preset().matrix.routes.size() == 5);

  const auto has_route = [&](const char* input, const char* output) {
    for (const auto& route : restored.preset().matrix.routes) {
      if (route.input_id == input && route.output_id == output) {
        return true;
      }
    }
    return false;
  };
  assert(has_route("asio-output-l", "wasapi-render-l"));
  assert(has_route("asio-output-r", "wasapi-render-r"));
  assert(has_route("wasapi-capture-l", "asio-input-l"));
  assert(has_route("wasapi-capture-r", "asio-input-r"));
  assert(has_route("asio-output-r", "asio-input-r"));

  second.endpoints[0].clock_master = true;
  auto invalid = sar::service::reconcile_audio_runtime_matrix_preset(
      result.preset(), first, second);
  assert(!invalid.ok());
  assert(!invalid.errors().empty());
}
