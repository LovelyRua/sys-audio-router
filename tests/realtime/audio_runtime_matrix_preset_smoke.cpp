#include "core/service/audio_runtime_matrix_preset.h"

#include <algorithm>
#include <cassert>
#include <string_view>

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

bool has_port(const std::vector<sar::graph::RouteEndpointDescriptor>& ports,
              std::string_view id) {
  return std::ranges::any_of(ports, [&](const auto& port) {
    return port.id == id;
  });
}

const sar::control::PresetRoute* find_route(
    const sar::control::PresetDocument& preset,
    std::string_view input,
    std::string_view output) {
  const auto found = std::ranges::find_if(preset.matrix.routes,
                                          [&](const auto& route) {
    return route.input_id == input && route.output_id == output;
  });
  return found == preset.matrix.routes.end() ? nullptr : &*found;
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

  auto with_other_ports = result.preset();
  with_other_ports.matrix.inputs.push_back({"foreign.ch1", "External Input"});
  with_other_ports.matrix.outputs.push_back({"foreign.ch2", "External Output"});
  with_other_ports.matrix.routes.push_back(
      {"foreign.ch1", "foreign.ch2", 0.25F, true});
  with_other_ports.matrix.routes.push_back(
      {"capture-b.ch1", "render-main.ch1", 0.5F, true});
  with_other_ports.matrix.routes.push_back(
      {"capture-a.ch1", "render-b.ch1", 0.75F, false});
  const auto retained = sar::service::reconcile_audio_runtime_matrix_preset(
      with_other_ports, first, second);
  assert(retained.ok());
  assert(has_port(retained.preset().matrix.inputs, "foreign.ch1"));
  assert(has_port(retained.preset().matrix.outputs, "foreign.ch2"));
  assert(!has_port(retained.preset().matrix.inputs, "capture-a.ch1"));
  assert(!has_port(retained.preset().matrix.outputs, "render-b.ch1"));
  const auto* foreign_route = find_route(retained.preset(), "foreign.ch1",
                                         "foreign.ch2");
  assert(foreign_route != nullptr);
  assert(foreign_route->gain == 0.25F && foreign_route->muted);
  const auto* retained_route = find_route(retained.preset(), "capture-b.ch1",
                                          "render-main.ch1");
  assert(retained_route != nullptr);
  assert(retained_route->gain == 0.5F && retained_route->muted);
  assert(find_route(retained.preset(), "capture-a.ch1", "render-b.ch1") ==
         nullptr);

  // Unknown historical ports are indistinguishable from other backends.
  const auto restored_without_history =
      sar::service::reconcile_audio_runtime_matrix_preset(
          with_other_ports, {}, second);
  assert(restored_without_history.ok());
  assert(has_port(restored_without_history.preset().matrix.inputs,
                  "capture-a.ch1"));
  assert(has_port(restored_without_history.preset().matrix.outputs,
                  "render-b.ch1"));
  assert(find_route(restored_without_history.preset(), "capture-a.ch1",
                    "render-b.ch1") != nullptr);

  using Backend = sar::control::AudioRuntimeEndpointBackend;
  auto mixed_first = first;
  mixed_first.endpoints[2].clock_master = false;
  mixed_first.endpoints.push_back(
      {"asio-capture", "asio-driver", Direction::Capture, false, 0, 2,
       Backend::PhysicalAsio, "asio-group", 48000, 128});
  mixed_first.endpoints.push_back(
      {"asio-render", "asio-driver", Direction::Render, true, 0, 2,
       Backend::PhysicalAsio, "asio-group", 48000, 128});
  auto mixed_initial = sar::service::reconcile_audio_runtime_matrix_preset(
      make_alpha_preset(), {}, mixed_first);
  assert(mixed_initial.ok());
  auto mixed_preset = mixed_initial.take_preset();
  mixed_preset.matrix.routes.push_back(
      {"asio-capture.ch1", "asio-render.ch1", 0.375F, true});
  auto mixed_next = mixed_first;
  std::erase_if(mixed_next.endpoints, [](const auto& endpoint) {
    return endpoint.endpoint_id == "capture-a" ||
           endpoint.endpoint_id == "render-b";
  });
  const auto mixed_reconfigured =
      sar::service::reconcile_audio_runtime_matrix_preset(
          mixed_preset, mixed_first, mixed_next);
  assert(mixed_reconfigured.ok());
  assert(has_port(mixed_reconfigured.preset().matrix.inputs,
                  "asio-capture.ch1"));
  assert(has_port(mixed_reconfigured.preset().matrix.outputs,
                  "asio-render.ch1"));
  const auto* asio_route = find_route(mixed_reconfigured.preset(),
                                      "asio-capture.ch1", "asio-render.ch1");
  assert(asio_route != nullptr);
  assert(asio_route->gain == 0.375F && asio_route->muted);

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
