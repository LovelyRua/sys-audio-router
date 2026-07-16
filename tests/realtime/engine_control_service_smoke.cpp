#include "core/service/engine_control_service.h"

#include <cassert>

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 64;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input", "Input"});
  preset.matrix.outputs.push_back({"output", "Output"});
  preset.matrix.routes.push_back({"input", "output", 1.0F, false});
  return preset;
}

sar::control::ControlResponse send(
    sar::service::EngineControlService& service,
    const sar::control::ControlCommand& command) {
  const auto request = sar::control::encode_control_command(command);
  assert(request.ok());
  const auto response_bytes = service.handle_wire_request(request.bytes);
  assert(response_bytes.ok());
  const auto response =
      sar::control::decode_control_response(response_bytes.bytes);
  assert(response.ok());
  return response.response;
}

}  // namespace

int main() {
  auto create = sar::service::EngineControlService::create(make_preset(), 10);
  assert(create.ok());
  auto service = create.take_service();

  sar::control::ControlCommand state;
  state.command_id = "state-1";
  state.type = sar::control::ControlCommandType::QuerySessionState;
  const auto before = send(*service, state);
  assert(before.status == sar::control::ControlResponseStatus::Accepted);
  assert(before.has_session_state);
  assert(before.next_graph_version == 11);
  assert(before.preset.matrix.routes[0].gain == 1.0F);

  sar::control::ControlCommand gain;
  gain.command_id = "gain-1";
  gain.type = sar::control::ControlCommandType::SetGain;
  gain.input_id = "input";
  gain.output_id = "output";
  gain.gain = 0.25F;
  const auto applied = send(*service, gain);
  assert(applied.status == sar::control::ControlResponseStatus::Accepted);

  state.command_id = "state-2";
  const auto after = send(*service, state);
  assert(after.next_graph_version == 12);
  assert(after.preset.matrix.routes[0].gain == 0.25F);

  const std::uint8_t malformed[] = {0, 1, 2};
  const auto rejected_bytes = service->handle_wire_request(malformed);
  assert(rejected_bytes.ok());
  const auto rejected =
      sar::control::decode_control_response(rejected_bytes.bytes);
  assert(rejected.ok());
  assert(rejected.response.status ==
         sar::control::ControlResponseStatus::Rejected);
  assert(rejected.response.errors[0].code == "invalid_control_wire_request");
}
