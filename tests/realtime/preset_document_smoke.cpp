#include "core/control/preset_document.h"

#include "core/diagnostics/engine_diagnostics.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

bool has_error_code(const sar::control::PresetValidationResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

bool has_error_code(const sar::control::PresetMatrixBuildResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

bool has_error_code(const sar::control::PresetGraphBuildResult& result,
                    const std::string& code) {
  for (const auto& error : result.errors()) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

sar::control::PresetDocument make_valid_preset() {
  sar::control::PresetDocument preset;
  preset.schema_version = 1;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"mic_left", "Mic Left"});
  preset.matrix.inputs.push_back({"mic_right", "Mic Right"});
  preset.matrix.outputs.push_back({"monitor_left", "Monitor Left"});
  preset.matrix.outputs.push_back({"monitor_right", "Monitor Right"});
  preset.matrix.routes.push_back({"mic_left", "monitor_left", 1.0F});
  preset.matrix.routes.push_back({"mic_right", "monitor_right", 1.0F});
  return preset;
}

}  // namespace

int main() {
  {
    const auto preset = make_valid_preset();
    const auto result = sar::control::validate_preset(preset);
    if (const auto failure = expect(result.ok(), "Expected valid preset")) {
      return failure;
    }
  }

  {
    const auto preset = make_valid_preset();
    auto result = sar::control::build_route_matrix(preset);
    if (const auto failure = expect(result.ok(), "Expected route matrix build success")) {
      return failure;
    }

    auto matrix = result.take_matrix();
    if (const auto failure = expect(matrix != nullptr, "Expected route matrix pointer")) {
      return failure;
    }
    if (const auto failure = expect(matrix->input_id(0) == std::string_view{"mic_left"},
                                    "Expected matrix input ID from preset")) {
      return failure;
    }
    if (const auto failure = expect(matrix->output_id(1) == std::string_view{"monitor_right"},
                                    "Expected matrix output ID from preset")) {
      return failure;
    }

    sar::realtime::AudioBuffer input(2, 2);
    sar::realtime::AudioBuffer output(2, 2);
    input.channel(0)[0] = 0.25F;
    input.channel(0)[1] = 0.5F;
    input.channel(1)[0] = 0.75F;
    input.channel(1)[1] = 1.0F;

    matrix->process(input, output);
    if (!sar::tests::nearly_equal(output.channel(0)[0], 0.25F) ||
        !sar::tests::nearly_equal(output.channel(0)[1], 0.5F) ||
        !sar::tests::nearly_equal(output.channel(1)[0], 0.75F) ||
        !sar::tests::nearly_equal(output.channel(1)[1], 1.0F)) {
      std::cerr << "Unexpected preset-built matrix output\n";
      return 1;
    }
  }

  {
    const auto preset = make_valid_preset();
    auto result = sar::control::build_preset_graph(preset, 42);
    if (const auto failure = expect(result.ok(), "Expected preset graph build success")) {
      return failure;
    }

    auto graph = result.take_graph();
    if (const auto failure = expect(graph != nullptr, "Expected preset graph pointer")) {
      return failure;
    }
    if (const auto failure = expect(graph->version() == 42, "Expected preset graph version")) {
      return failure;
    }
    if (const auto failure = expect(graph->sample_rate() == preset.sample_rate,
                                    "Expected preset graph sample rate")) {
      return failure;
    }
    if (const auto failure = expect(graph->frames() == preset.frames_per_block,
                                    "Expected preset graph block size")) {
      return failure;
    }
    if (const auto failure = expect(graph->channels() == preset.matrix.outputs.size(),
                                    "Expected preset graph output channel count")) {
      return failure;
    }
    if (const auto failure = expect(graph->node_count() == 1,
                                    "Expected preset graph matrix node")) {
      return failure;
    }
    if (const auto failure = expect(graph->node_id(0) == std::string_view{"matrix"},
                                    "Expected preset graph node ID")) {
      return failure;
    }

    sar::realtime::AudioBuffer input(2, preset.frames_per_block);
    sar::realtime::AudioBuffer output(2, preset.frames_per_block);
    sar::diagnostics::EngineDiagnostics diagnostics;
    input.channel(0)[0] = 0.125F;
    input.channel(1)[0] = 0.875F;

    graph->process(input, output, diagnostics);
    if (!sar::tests::nearly_equal(output.channel(0)[0], 0.125F) ||
        !sar::tests::nearly_equal(output.channel(1)[0], 0.875F) ||
        diagnostics.graph_version != 42 ||
        diagnostics.processed_blocks != 1) {
      std::cerr << "Unexpected preset-built graph output\n";
      return 1;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.schema_version = 0;
    preset.sample_rate = 0;
    preset.frames_per_block = 0;
    const auto result = sar::control::validate_preset(preset);
    if (const auto failure = expect(!result.ok(), "Expected invalid preset timing")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_schema_version"),
                                    "Expected invalid_schema_version error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_sample_rate"),
                                    "Expected invalid_sample_rate error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_frames_per_block"),
                                    "Expected invalid_frames_per_block error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.nodes.push_back({"matrix", "Duplicate Matrix", "route_matrix"});
    const auto result = sar::control::validate_preset(preset);
    if (const auto failure = expect(!result.ok(), "Expected duplicate node failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_node_id"),
                                    "Expected duplicate_node_id error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.matrix.routes.push_back({"missing", "monitor_left", 1.0F});
    preset.matrix.routes.push_back({"mic_left", "missing", 1.0F});
    const auto result = sar::control::validate_preset(preset);
    if (const auto failure = expect(!result.ok(), "Expected unknown route endpoint failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unknown_route_input"),
                                    "Expected unknown_route_input error")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unknown_route_output"),
                                    "Expected unknown_route_output error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.matrix.routes.push_back({
        "mic_left",
        "monitor_left",
        std::numeric_limits<float>::infinity(),
    });
    const auto result = sar::control::validate_preset(preset);
    if (const auto failure = expect(!result.ok(), "Expected invalid route gain failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "invalid_route_gain"),
                                    "Expected invalid_route_gain error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.matrix.routes[0].muted = true;
    auto result = sar::control::build_route_matrix(preset);
    if (const auto failure = expect(result.ok(), "Expected muted route matrix build success")) {
      return failure;
    }

    auto matrix = result.take_matrix();
    sar::realtime::AudioBuffer input(2, 1);
    sar::realtime::AudioBuffer output(2, 1);
    input.channel(0)[0] = 1.0F;
    input.channel(1)[0] = 1.0F;
    matrix->process(input, output);
    if (!sar::tests::nearly_equal(output.channel(0)[0], 0.0F) ||
        !sar::tests::nearly_equal(output.channel(1)[0], 1.0F)) {
      std::cerr << "Unexpected muted preset-built matrix output\n";
      return 1;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.matrix.routes.push_back({"mic_left", "monitor_left", 0.5F});
    const auto result = sar::control::build_route_matrix(preset);
    if (const auto failure = expect(!result.ok(), "Expected duplicate route build failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "duplicate_route"),
                                    "Expected duplicate_route error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.nodes.push_back({"gain", "Gain", "gain"});
    const auto result = sar::control::build_preset_graph(preset);
    if (const auto failure = expect(!result.ok(), "Expected unsupported node graph failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "unsupported_preset_node_type"),
                                    "Expected unsupported_preset_node_type error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.nodes.clear();
    const auto result = sar::control::build_preset_graph(preset);
    if (const auto failure = expect(!result.ok(), "Expected missing matrix node graph failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "missing_route_matrix_node"),
                                    "Expected missing_route_matrix_node error")) {
      return failure;
    }
  }

  {
    auto preset = make_valid_preset();
    preset.matrix.outputs.push_back({"broadcast", "Broadcast"});
    const auto result = sar::control::build_preset_graph(preset);
    if (const auto failure = expect(!result.ok(), "Expected asymmetric graph failure")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(result, "asymmetric_matrix_channel_count"),
                                    "Expected asymmetric_matrix_channel_count error")) {
      return failure;
    }
  }

  std::cout << "Preset document smoke test passed\n";
  return 0;
}
