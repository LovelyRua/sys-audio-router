#include "app/gui_vstgui/matrix_projection.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {

using namespace sar;
using control::AudioRuntimeConfiguration;
using control::AudioRuntimeEndpointBackend;
using control::AudioRuntimeEndpointConfiguration;
using control::AudioRuntimeEndpointDirection;
using control::AudioRuntimeMode;
using control::PresetRoute;
using control::PresetRouteMatrix;
using graph::RouteEndpointDescriptor;
using gui_vstgui::MatrixProjection;
using gui_vstgui::projectMatrix;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

void requireIds(const std::vector<RouteEndpointDescriptor>& endpoints,
                std::initializer_list<std::string_view> expected,
                const char* message) {
  require(endpoints.size() == expected.size(), message);
  auto id = expected.begin();
  for (const auto& endpoint : endpoints) {
    require(endpoint.id == *id++, message);
  }
}

void requireFlags(const std::vector<bool>& actual,
                  std::initializer_list<bool> expected, const char* message) {
  require(actual == std::vector<bool>(expected), message);
}

void requireRoutes(const std::vector<PresetRoute>& actual,
                   std::initializer_list<PresetRoute> expected,
                   const char* message) {
  require(actual.size() == expected.size(), message);
  auto route = expected.begin();
  for (const auto& item : actual) {
    require(item.input_id == route->input_id &&
                item.output_id == route->output_id && item.gain == route->gain &&
                item.muted == route->muted,
            message);
    ++route;
  }
}

bool containsId(const std::vector<RouteEndpointDescriptor>& endpoints,
                std::string_view id) {
  return std::any_of(endpoints.begin(), endpoints.end(),
                     [id](const auto& endpoint) { return endpoint.id == id; });
}

void requireSameEndpoints(const std::vector<RouteEndpointDescriptor>& actual,
                          const std::vector<RouteEndpointDescriptor>& expected) {
  require(actual.size() == expected.size(), "Endpoint count changed");
  for (std::size_t index = 0; index < actual.size(); ++index) {
    require(actual[index].id == expected[index].id &&
                actual[index].label == expected[index].label,
            "Endpoint data changed");
  }
}

void requireSameMatrix(const PresetRouteMatrix& actual,
                       const PresetRouteMatrix& expected) {
  requireSameEndpoints(actual.inputs, expected.inputs);
  requireSameEndpoints(actual.outputs, expected.outputs);
  require(actual.routes.size() == expected.routes.size(), "Route count changed");
  for (std::size_t index = 0; index < actual.routes.size(); ++index) {
    const auto& a = actual.routes[index];
    const auto& b = expected.routes[index];
    require(a.input_id == b.input_id && a.output_id == b.output_id &&
                a.gain == b.gain && a.muted == b.muted,
            "Route data changed");
  }
}

void requireSameRuntime(const AudioRuntimeConfiguration& actual,
                        const AudioRuntimeConfiguration& expected) {
  require(actual.mode == expected.mode &&
              actual.capture_device_id == expected.capture_device_id &&
              actual.render_device_id == expected.render_device_id &&
              actual.physical_asio_driver_clsid ==
                  expected.physical_asio_driver_clsid &&
              actual.physical_asio_sample_rate == expected.physical_asio_sample_rate &&
              actual.physical_asio_block_frames == expected.physical_asio_block_frames &&
              actual.physical_asio_input_channels == expected.physical_asio_input_channels &&
              actual.physical_asio_output_channels == expected.physical_asio_output_channels &&
              actual.endpoints.size() == expected.endpoints.size(),
          "Runtime configuration changed");
  for (std::size_t index = 0; index < actual.endpoints.size(); ++index) {
    const auto& a = actual.endpoints[index];
    const auto& b = expected.endpoints[index];
    require(a.endpoint_id == b.endpoint_id && a.device_id == b.device_id &&
                a.direction == b.direction && a.clock_master == b.clock_master &&
                a.first_channel == b.first_channel &&
                a.channel_count == b.channel_count && a.backend == b.backend &&
                a.device_group_id == b.device_group_id &&
                a.sample_rate == b.sample_rate && a.block_frames == b.block_frames,
            "Runtime endpoint changed");
  }
}

AudioRuntimeEndpointConfiguration endpoint(
    const char* id, AudioRuntimeEndpointDirection direction, std::uint32_t count,
    AudioRuntimeEndpointBackend backend = AudioRuntimeEndpointBackend::Wasapi) {
  AudioRuntimeEndpointConfiguration result;
  result.endpoint_id = id;
  result.device_id = "native-device";
  result.direction = direction;
  result.channel_count = count;
  result.backend = backend;
  return result;
}

void checkMatrixProjection() {
  PresetRouteMatrix matrix;
  matrix.inputs = {
      {"mic.ch2", "Microphone second"},
      {"BGM.ch1", "Background"},
      {"mic.ch1", "Microphone first"},
      {"mic.ch3", "Outside capture range"},
      {"speaker.ch1", "Wrong direction"},
      {"asio-output-1", "ASIO DAW Out"},
      {"virtual-wasapi-lab-1.ch1", "Lab input"},
      {"custom-source", "Custom source"},
      {"physical.ch2", "Physical ASIO"},
      {"mic.ch01", "Leading zero"},
      {"mic.ch0", "Zero"},
      {"mic.ch1x", "Non-channel suffix"},
      {"wide.ch4294967295", "Maximum channel number"},
      {"wide.ch4294967296", "Channel number overflow"},
      {"wide.ch999999999999999999999999999999", "Long overflow"},
      {"virtual-wasapi-lab-01.ch1", "Invalid lab index"},
      {"virtual-wasapi-lab-999999999999999999999.ch999999999999999999999",
       "Large lab input"},
      {"empty.ch1", "Zero channels"},
      {"mic.ch2.extra", "Custom trailing text"},
      {"mix.ch2.ch1", "Runtime ID contains channel text"},
  };
  matrix.outputs = {
      {"speaker.ch2", "Speaker second"},
      {"aux.ch1", "Auxiliary"},
      {"mic.ch1", "Wrong direction"},
      {"asio-input-1", "ASIO DAW In"},
      {"virtual-wasapi-lab-1.ch1", "Not a lab input"},
      {"physical-out.ch1", "Physical ASIO output"},
      {"wide.ch4294967295", "Capture only"},
      {"speaker.ch01", "Custom leading zero"},
  };
  matrix.routes = {
      {"mic.ch2", "speaker.ch2", 0.25F, true},
      {"BGM.ch1", "speaker.ch2", 0.5F, false},
      {"mic.ch1", "aux.ch1", 0.75F, true},
      {"physical.ch2", "physical-out.ch1", 1.25F, true},
      {"missing-source", "speaker.ch2", 1.0F, false},
      {"mic.ch1", "missing-output", 1.0F, false},
      {"virtual-wasapi-lab-1.ch1", "asio-input-1", 0.125F, false},
      {"mic.ch01", "speaker.ch01", 0.0625F, true},
      {"wide.ch4294967296", "speaker.ch2", 0.5F, true},
      {"wide.ch4294967295", "speaker.ch2", 1.0F, false},
  };
  const auto original = matrix;

  AudioRuntimeConfiguration runtime;
  runtime.mode = AudioRuntimeMode::WasapiMatrix;
  runtime.capture_device_id = "legacy-capture";
  runtime.render_device_id = "legacy-render";
  runtime.physical_asio_driver_clsid = "preserved-driver";
  runtime.physical_asio_input_channels = {1, 3};
  runtime.endpoints = {
      endpoint("speaker", AudioRuntimeEndpointDirection::Render, 2),
      endpoint("mic", AudioRuntimeEndpointDirection::Capture, 1),
      endpoint("mic", AudioRuntimeEndpointDirection::Capture, 2),
      endpoint("physical", AudioRuntimeEndpointDirection::Capture, 2,
               AudioRuntimeEndpointBackend::PhysicalAsio),
      endpoint("physical-out", AudioRuntimeEndpointDirection::Render, 1,
               AudioRuntimeEndpointBackend::PhysicalAsio),
      endpoint("wide", AudioRuntimeEndpointDirection::Capture,
               std::numeric_limits<std::uint32_t>::max()),
      endpoint("empty", AudioRuntimeEndpointDirection::Capture, 0),
      endpoint("mix.ch2", AudioRuntimeEndpointDirection::Capture, 1),
  };
  runtime.endpoints[2].first_channel = 12;
  const auto originalRuntime = runtime;

  auto visible = projectMatrix(matrix, runtime, true, false);
  requireIds(visible.matrix.inputs,
             {"mic.ch2", "mic.ch1", "asio-output-1", "virtual-wasapi-lab-1.ch1",
              "custom-source", "physical.ch2", "mic.ch01", "mic.ch0", "mic.ch1x",
              "wide.ch4294967295",
              "virtual-wasapi-lab-999999999999999999999.ch999999999999999999999",
              "mic.ch2.extra", "mix.ch2.ch1"},
             "Matrix input binding or ordering is wrong");
  requireIds(visible.matrix.outputs,
             {"speaker.ch2", "asio-input-1", "physical-out.ch1", "speaker.ch01"},
             "Matrix output binding or ordering is wrong");
  require(std::all_of(visible.inputActive.begin(), visible.inputActive.end(),
                      [](bool active) { return active; }) &&
              visible.inputActive.size() == visible.matrix.inputs.size() &&
              std::all_of(visible.outputActive.begin(), visible.outputActive.end(),
                          [](bool active) { return active; }) &&
              visible.outputActive.size() == visible.matrix.outputs.size(),
          "Hidden inactive endpoints left misaligned flags");
  requireRoutes(visible.matrix.routes,
                {{"mic.ch2", "speaker.ch2", 0.25F, true},
                 {"physical.ch2", "physical-out.ch1", 1.25F, true},
                 {"virtual-wasapi-lab-1.ch1", "asio-input-1", 0.125F, false},
                 {"mic.ch01", "speaker.ch01", 0.0625F, true},
                 {"wide.ch4294967295", "speaker.ch2", 1.0F, false}},
                "Visible routes lost their order or properties");
  require(visible.matrix.inputs.front().label == "Microphone second",
          "Projection changed an endpoint label");

  const auto all = projectMatrix(matrix, runtime, true, true);
  requireSameEndpoints(all.matrix.inputs, matrix.inputs);
  requireSameEndpoints(all.matrix.outputs, matrix.outputs);
  requireFlags(all.inputActive,
               {true, false, true, false, false, true, true, true, true, true,
                true, true, true, false, false, false, true, false, true, true},
               "Matrix input active flags are wrong");
  requireFlags(all.outputActive, {true, false, false, true, false, true, false, true},
               "Matrix output active flags are wrong");
  requireRoutes(all.matrix.routes,
                {{"mic.ch2", "speaker.ch2", 0.25F, true},
                 {"BGM.ch1", "speaker.ch2", 0.5F, false},
                 {"mic.ch1", "aux.ch1", 0.75F, true},
                 {"physical.ch2", "physical-out.ch1", 1.25F, true},
                 {"virtual-wasapi-lab-1.ch1", "asio-input-1", 0.125F, false},
                 {"mic.ch01", "speaker.ch01", 0.0625F, true},
                 {"wide.ch4294967296", "speaker.ch2", 0.5F, true},
                 {"wide.ch4294967295", "speaker.ch2", 1.0F, false}},
                "Showing inactive endpoints filtered or changed routes");

  AudioRuntimeConfiguration changed;
  changed.mode = AudioRuntimeMode::WasapiMatrix;
  changed.endpoints = {
      endpoint("BGM", AudioRuntimeEndpointDirection::Capture, 1),
      endpoint("aux", AudioRuntimeEndpointDirection::Render, 1),
  };
  const auto switched = projectMatrix(matrix, changed, true, false);
  require(containsId(switched.matrix.inputs, "BGM.ch1") &&
              !containsId(switched.matrix.inputs, "mic.ch1") &&
              containsId(switched.matrix.outputs, "aux.ch1") &&
              !containsId(switched.matrix.outputs, "speaker.ch2"),
          "Switching runtime did not update bindings");
  require(!containsId(switched.matrix.inputs, "physical.ch2") &&
              containsId(switched.matrix.inputs, "virtual-wasapi-lab-1.ch1"),
          "Runtime switch changed physical or lab input behavior");

  changed.endpoints.clear();
  const auto empty = projectMatrix(matrix, changed, true, false);
  require(!containsId(empty.matrix.inputs, "mic.ch1") &&
              !containsId(empty.matrix.outputs, "speaker.ch2") &&
              containsId(empty.matrix.inputs, "custom-source") &&
              containsId(empty.matrix.inputs, "virtual-wasapi-lab-1.ch1"),
          "Configured empty runtime did not preserve only custom and lab inputs");
  const auto unknown = projectMatrix(matrix, changed, false, false);
  requireSameEndpoints(unknown.matrix.inputs, matrix.inputs);
  requireSameEndpoints(unknown.matrix.outputs, matrix.outputs);
  require(std::all_of(unknown.inputActive.begin(), unknown.inputActive.end(),
                      [](bool active) { return active; }) &&
              std::all_of(unknown.outputActive.begin(), unknown.outputActive.end(),
                          [](bool active) { return active; }),
          "Unknown matrix configuration hid endpoints");

  visible.matrix.inputs.front().label = "changed projection";
  visible.matrix.routes.front().gain = 0.0F;
  visible.matrix.routes.front().muted = false;
  requireSameMatrix(matrix, original);
  requireSameRuntime(runtime, originalRuntime);
}

void checkSparseChannels() {
  PresetRouteMatrix matrix;
  matrix.inputs = {{"capture.ch7", "Seventh"}, {"capture.ch3", "Third"},
                   {"capture.ch8", "Out of range"}};
  matrix.outputs = {{"render.ch5", "Fifth"}, {"render.ch2", "Second"},
                    {"render.ch6", "Out of range"}};
  matrix.routes = {{"capture.ch7", "render.ch2", 0.5F, true},
                   {"capture.ch3", "render.ch5", 0.25F, false},
                   {"capture.ch8", "render.ch5", 1.0F, false},
                   {"capture.ch3", "render.ch6", 1.0F, false}};
  const auto original = matrix;
  AudioRuntimeConfiguration runtime;
  runtime.mode = AudioRuntimeMode::WasapiMatrix;
  runtime.endpoints = {
      endpoint("capture", AudioRuntimeEndpointDirection::Capture, 7),
      endpoint("render", AudioRuntimeEndpointDirection::Render, 5),
  };
  runtime.endpoints[0].first_channel = 10;
  const auto visible = projectMatrix(matrix, runtime, true, false);
  requireIds(visible.matrix.inputs, {"capture.ch7", "capture.ch3"},
             "Sparse input channels were renumbered or reordered");
  requireIds(visible.matrix.outputs, {"render.ch5", "render.ch2"},
             "Sparse output channels were renumbered or reordered");
  requireRoutes(visible.matrix.routes,
                {{"capture.ch7", "render.ch2", 0.5F, true},
                 {"capture.ch3", "render.ch5", 0.25F, false}},
                "Sparse channel routes were not preserved");
  const auto all = projectMatrix(matrix, runtime, true, true);
  requireFlags(all.inputActive, {true, true, false}, "Sparse input flags are wrong");
  requireFlags(all.outputActive, {true, true, false}, "Sparse output flags are wrong");
  requireSameMatrix(matrix, original);
}

void checkLegacyProjection() {
  PresetRouteMatrix fallback;
  fallback.inputs = {{"a", "One"}, {"b", "Two"}, {"c", "Three"}, {"d", "Four"}};
  fallback.outputs = {{"e", "One"}, {"f", "Two"}, {"g", "Three"}, {"h", "Four"}};
  fallback.routes = {{"a", "e", 0.25F, false}, {"c", "e", 0.5F, true},
                     {"d", "h", 0.75F, false}};
  const auto original = fallback;
  AudioRuntimeConfiguration runtime;
  for (const auto mode : {AudioRuntimeMode::WasapiRender,
                          AudioRuntimeMode::PhysicalAsio, AudioRuntimeMode::None}) {
    runtime.mode = mode;
    const auto visible = projectMatrix(fallback, runtime, true, false);
    requireIds(visible.matrix.inputs, {"c", "d"}, "Legacy input fallback is wrong");
    requireIds(visible.matrix.outputs, {"e", "f"}, "Legacy output fallback is wrong");
    requireRoutes(visible.matrix.routes, {{"c", "e", 0.5F, true}},
                  "Legacy projection retained a hidden route");
    const auto all = projectMatrix(fallback, runtime, false, true);
    requireFlags(all.inputActive, {false, false, true, true},
                 "Legacy input fallback flags are wrong");
    requireFlags(all.outputActive, {true, true, false, false},
                 "Legacy output fallback flags are wrong");
    requireSameEndpoints(all.matrix.inputs, fallback.inputs);
    requireSameEndpoints(all.matrix.outputs, fallback.outputs);
  }

  PresetRouteMatrix families;
  families.inputs = {{"one", "HARDWARE"}, {"two", "Capture"},
                     {"three", "ReNdEr"}, {"four", "monitor"},
                     {"five", "WASAPI"}, {"six", "Virtual WASAPI"},
                     {"seven", "DAW"}, {"eight", "ASIO"},
                     {"nine", "custom"}, {"Wasapi-ASIO", "precedence"}};
  families.outputs = families.inputs;
  runtime.mode = AudioRuntimeMode::WasapiRender;
  const auto classified = projectMatrix(families, runtime, true, true);
  requireFlags(classified.inputActive,
               {false, false, false, false, false, true, true, true, true, true},
               "Legacy input family inference is wrong");
  requireFlags(classified.outputActive,
               {true, true, true, true, true, false, false, false, true, false},
               "Legacy output family inference is wrong");

  runtime.mode = AudioRuntimeMode::WasapiDuplex;
  const auto duplex = projectMatrix(fallback, runtime, true, false);
  requireSameMatrix(duplex.matrix, fallback);
  requireFlags(duplex.inputActive, {true, true, true, true},
               "Duplex input was hidden");
  requireFlags(duplex.outputActive, {true, true, true, true},
               "Duplex output was hidden");
  requireSameMatrix(fallback, original);

  const auto empty = projectMatrix({}, runtime, true, true);
  require(empty.matrix.inputs.empty() && empty.matrix.outputs.empty() &&
              empty.matrix.routes.empty() && empty.inputActive.empty() &&
              empty.outputActive.empty(),
          "Empty matrix produced phantom endpoints or routes");
}

}  // namespace

int main() {
  checkMatrixProjection();
  checkSparseChannels();
  checkLegacyProjection();
  std::cout << "VSTGUI matrix projection checks passed\n";
}
