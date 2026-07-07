#include "core/control/control_session.h"

#include "core/diagnostics/engine_diagnostics.h"
#include "core/realtime/audio_buffer.h"
#include "tests/realtime/test_helpers.h"

#include <iostream>
#include <memory>
#include <string>

namespace {

int expect(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    return 1;
  }
  return 0;
}

bool has_error_code(const sar::control::ControlResponse& response,
                    const std::string& code) {
  for (const auto& error : response.errors) {
    if (error.code == code) {
      return true;
    }
  }
  return false;
}

sar::control::PresetDocument make_valid_preset() {
  sar::control::PresetDocument preset;
  preset.schema_version = 1;
  preset.sample_rate = 48000;
  preset.frames_per_block = 64;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"mic_l", "Mic L"});
  preset.matrix.inputs.push_back({"mic_r", "Mic R"});
  preset.matrix.outputs.push_back({"monitor_l", "Monitor L"});
  preset.matrix.outputs.push_back({"monitor_r", "Monitor R"});
  preset.matrix.routes.push_back({"mic_l", "monitor_l", 1.0F});
  preset.matrix.routes.push_back({"mic_r", "monitor_r", 1.0F});
  return preset;
}

void fill_input(sar::realtime::AudioBuffer& input) {
  for (std::size_t channel = 0; channel < input.channels(); ++channel) {
    auto samples = input.channel(channel);
    for (std::size_t frame = 0; frame < input.frames(); ++frame) {
      samples[frame] = static_cast<float>((channel + 1) * 10 + frame);
    }
  }
}

int expect_output_gain(const sar::realtime::AudioBuffer& input,
                       const sar::realtime::AudioBuffer& output,
                       float left_gain,
                       float right_gain,
                       const char* message) {
  const float gains[] = {left_gain, right_gain};
  for (std::size_t channel = 0; channel < output.channels(); ++channel) {
    const auto in = input.channel(channel);
    const auto out = output.channel(channel);
    for (std::size_t frame = 0; frame < output.frames(); ++frame) {
      if (!sar::tests::nearly_equal(out[frame], in[frame] * gains[channel])) {
        return sar::tests::fail_sample(message, channel, frame);
      }
    }
  }
  return 0;
}

}  // namespace

int main() {
  auto create_result = sar::control::ControlSession::create(make_valid_preset(), 10);
  if (const auto failure = expect(create_result.ok(), "Expected control session creation")) {
    return failure;
  }

  auto session = create_result.take_session();
  if (const auto failure = expect(session != nullptr, "Expected session pointer")) {
    return failure;
  }
  if (const auto failure = expect(session->current_graph() != nullptr,
                                  "Expected initial graph")) {
    return failure;
  }
  if (const auto failure = expect(session->current_graph()->version() == 10,
                                  "Expected initial graph version")) {
    return failure;
  }
  if (const auto failure = expect(session->next_graph_version() == 11,
                                  "Expected next graph version")) {
    return failure;
  }

  sar::realtime::AudioBuffer input(2, 64);
  sar::realtime::AudioBuffer output(2, 64);
  sar::diagnostics::EngineDiagnostics diagnostics;
  fill_input(input);

  session->process(input, output, diagnostics);
  if (const auto failure = expect(diagnostics.graph_version == 10,
                                  "Expected diagnostics graph version from session")) {
    return failure;
  }
  if (const auto failure = expect_output_gain(input,
                                              output,
                                              1.0F,
                                              1.0F,
                                              "Unexpected initial session output")) {
    return failure;
  }

  {
    sar::control::ControlCommand query;
    query.command_id = "graph_1";
    query.type = sar::control::ControlCommandType::QueryActiveGraph;
    const auto response = session->handle(query);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected active graph query accepted")) {
      return failure;
    }
    if (const auto failure = expect(response.has_active_graph,
                                    "Expected active graph payload")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.version == 10,
                                    "Expected active graph response version")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.nodes.size() == 1,
                                    "Expected one active graph node")) {
      return failure;
    }
  }

  {
    sar::diagnostics::EngineDiagnostics query_diagnostics;
    query_diagnostics.graph_version = 10;
    query_diagnostics.xrun_count = 2;

    sar::control::ControlCommand query;
    query.command_id = "diagnostics_1";
    query.type = sar::control::ControlCommandType::QueryDiagnostics;
    const auto response = session->handle(query, query_diagnostics);
    if (const auto failure = expect(response.has_diagnostics,
                                    "Expected diagnostics response payload")) {
      return failure;
    }
    if (const auto failure = expect(response.diagnostics.xrun_count == 2,
                                    "Expected diagnostics response xrun count")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand set_gain;
    set_gain.command_id = "gain_1";
    set_gain.type = sar::control::ControlCommandType::SetGain;
    set_gain.input_id = "mic_l";
    set_gain.output_id = "monitor_l";
    set_gain.gain = 0.25F;
    const auto response = session->handle(set_gain);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected set gain accepted")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 11,
                                    "Expected graph republish after set gain")) {
      return failure;
    }
    if (const auto failure = expect(session->next_graph_version() == 12,
                                    "Expected next graph version after set gain")) {
      return failure;
    }

    output.clear();
    session->process(input, output, diagnostics);
    if (const auto failure = expect(diagnostics.graph_version == 11,
                                    "Expected diagnostics after set gain")) {
      return failure;
    }
    if (const auto failure = expect_output_gain(input,
                                                output,
                                                0.25F,
                                                1.0F,
                                                "Unexpected set gain output")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand bad_connect;
    bad_connect.command_id = "bad_connect";
    bad_connect.type = sar::control::ControlCommandType::ConnectRoute;
    bad_connect.input_id = "missing";
    bad_connect.output_id = "monitor_l";
    bad_connect.gain = 1.0F;
    const auto response = session->handle(bad_connect);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Rejected,
                                    "Expected bad connect rejected")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(response, "unknown_route_input"),
                                    "Expected unknown_route_input response error")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 11,
                                    "Expected graph version preserved after rejection")) {
      return failure;
    }
  }

  {
    sar::control::PresetDocument preset = make_valid_preset();
    preset.matrix.routes[0].muted = true;

    sar::control::ControlCommand load;
    load.command_id = "load_1";
    load.type = sar::control::ControlCommandType::LoadPreset;
    load.preset = preset;
    const auto response = session->handle(load);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected load preset accepted")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 12,
                                    "Expected graph republish after load preset")) {
      return failure;
    }

    output.clear();
    session->process(input, output, diagnostics);
    if (const auto failure = expect_output_gain(input,
                                                output,
                                                0.0F,
                                                1.0F,
                                                "Unexpected loaded preset output")) {
      return failure;
    }
  }

  std::cout << "Control session smoke test passed\n";
  return 0;
}
