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
    sar::control::ControlCommand create;
    create.command_id = "endpoint_create_1";
    create.type = sar::control::ControlCommandType::CreateVirtualEndpoint;
    create.endpoint_id = "sar_asio_1";
    create.endpoint_label = "SAR ASIO 1";
    const auto response = session->handle(create);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected endpoint create accepted")) {
      return failure;
    }
    if (const auto failure = expect(session->virtual_endpoints().size() == 1,
                                    "Expected one virtual endpoint")) {
      return failure;
    }
    if (const auto failure = expect(session->virtual_endpoints()[0].format.sample_rate == 48000,
                                    "Expected endpoint sample rate from preset")) {
      return failure;
    }
    if (const auto failure = expect(session->virtual_endpoints()[0].format.channels == 2,
                                    "Expected endpoint channels from preset")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 10,
                                    "Expected endpoint create not to republish graph")) {
      return failure;
    }

    sar::control::ControlCommand list;
    list.command_id = "endpoint_list_1";
    list.type = sar::control::ControlCommandType::ListDevices;
    const auto list_response = session->handle(list);
    if (const auto failure = expect(list_response.has_devices,
                                    "Expected list devices payload")) {
      return failure;
    }
    if (const auto failure = expect(list_response.devices.size() == 1,
                                    "Expected one listed virtual device")) {
      return failure;
    }
    if (const auto failure = expect(list_response.devices[0].id == "sar_asio_1",
                                    "Expected listed endpoint ID")) {
      return failure;
    }
    if (const auto failure = expect(list_response.devices[0].is_virtual,
                                    "Expected listed endpoint to be virtual")) {
      return failure;
    }

    const auto duplicate = session->handle(create);
    if (const auto failure = expect(duplicate.status ==
                                        sar::control::ControlResponseStatus::Rejected,
                                    "Expected duplicate endpoint rejected")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(duplicate, "duplicate_endpoint_id"),
                                    "Expected duplicate_endpoint_id error")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand save;
    save.command_id = "save_1";
    save.type = sar::control::ControlCommandType::SavePreset;
    const auto response = session->handle(save);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected save preset accepted")) {
      return failure;
    }
    if (const auto failure = expect(response.has_preset, "Expected save preset payload")) {
      return failure;
    }
    if (const auto failure = expect(response.preset.matrix.routes.size() == 2,
                                    "Expected saved preset routes")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand state;
    state.command_id = "state_1";
    state.type = sar::control::ControlCommandType::QuerySessionState;
    const auto response = session->handle(state);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected session state accepted")) {
      return failure;
    }
    if (const auto failure = expect(response.has_session_state,
                                    "Expected session state payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_preset, "Expected state preset payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_devices, "Expected state devices payload")) {
      return failure;
    }
    if (const auto failure = expect(response.has_active_graph,
                                    "Expected state graph payload")) {
      return failure;
    }
    if (const auto failure = expect(response.devices.size() == 1,
                                    "Expected state virtual device")) {
      return failure;
    }
    if (const auto failure = expect(response.active_graph.version == 10,
                                    "Expected state graph version")) {
      return failure;
    }
    if (const auto failure = expect(response.next_graph_version == 11,
                                    "Expected state next graph version")) {
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
    sar::control::ControlCommand remove;
    remove.command_id = "endpoint_remove_1";
    remove.type = sar::control::ControlCommandType::RemoveVirtualEndpoint;
    remove.endpoint_id = "sar_asio_1";
    const auto response = session->handle(remove);
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected endpoint remove accepted")) {
      return failure;
    }
    if (const auto failure = expect(session->virtual_endpoints().empty(),
                                    "Expected endpoint registry to be empty")) {
      return failure;
    }

    const auto missing = session->handle(remove);
    if (const auto failure = expect(missing.status ==
                                        sar::control::ControlResponseStatus::Rejected,
                                    "Expected missing endpoint remove rejected")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(missing, "unknown_endpoint_id"),
                                    "Expected unknown_endpoint_id error")) {
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
    sar::control::ControlCommand left_gain;
    left_gain.command_id = "batch_left_gain";
    left_gain.type = sar::control::ControlCommandType::SetGain;
    left_gain.input_id = "mic_l";
    left_gain.output_id = "monitor_l";
    left_gain.gain = 0.5F;

    sar::control::ControlCommand right_gain;
    right_gain.command_id = "batch_right_gain";
    right_gain.type = sar::control::ControlCommandType::SetGain;
    right_gain.input_id = "mic_r";
    right_gain.output_id = "monitor_r";
    right_gain.gain = 0.25F;

    const auto response = session->handle_batch("batch_gain", {left_gain, right_gain});
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Accepted,
                                    "Expected batch gain accepted")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 12,
                                    "Expected one graph publish for batch")) {
      return failure;
    }
    if (const auto failure = expect(session->next_graph_version() == 13,
                                    "Expected next graph version after batch")) {
      return failure;
    }

    output.clear();
    session->process(input, output, diagnostics);
    if (const auto failure = expect_output_gain(input,
                                                output,
                                                0.5F,
                                                0.25F,
                                                "Unexpected batch gain output")) {
      return failure;
    }
  }

  {
    sar::control::ControlCommand bad_gain;
    bad_gain.command_id = "batch_bad_gain";
    bad_gain.type = sar::control::ControlCommandType::SetGain;
    bad_gain.input_id = "missing";
    bad_gain.output_id = "monitor_l";
    bad_gain.gain = 1.0F;

    const auto response = session->handle_batch("batch_bad", {bad_gain});
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Rejected,
                                    "Expected bad batch rejected")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(response, "unknown_route"),
                                    "Expected unknown_route batch error")) {
      return failure;
    }
    if (const auto failure = expect(session->current_graph()->version() == 12,
                                    "Expected graph version preserved after bad batch")) {
      return failure;
    }
  }

  {
    const auto response = session->handle_batch("batch_empty", {});
    if (const auto failure = expect(response.status ==
                                        sar::control::ControlResponseStatus::Rejected,
                                    "Expected empty batch rejected")) {
      return failure;
    }
    if (const auto failure = expect(has_error_code(response, "empty_command_batch"),
                                    "Expected empty_command_batch error")) {
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
    if (const auto failure = expect(session->current_graph()->version() == 13,
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
