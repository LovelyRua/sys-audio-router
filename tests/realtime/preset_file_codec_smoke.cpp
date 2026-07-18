#include "core/control/control_response.h"
#include "core/control/control_wire_protocol.h"
#include "core/control/preset_file_codec.h"

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 64;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input", "Input"});
  preset.matrix.outputs.push_back({"output", "Output"});
  preset.matrix.routes.push_back({"input", "output", 0.5F, true});
  return preset;
}

}  // namespace

int main() {
  const auto preset = make_preset();
  const auto encoded = sar::control::encode_preset_file(preset);
  assert(encoded.ok());
  assert(!encoded.bytes().empty());

  const auto decoded = sar::control::decode_preset_file(encoded.bytes());
  assert(decoded.ok());
  assert(decoded.preset().sample_rate == 48000);
  assert(decoded.preset().frames_per_block == 64);
  assert(decoded.preset().matrix.routes.size() == 1);
  assert(decoded.preset().matrix.routes[0].gain == 0.5F);
  assert(decoded.preset().matrix.routes[0].muted);

  const std::vector<std::uint8_t> empty;
  const auto empty_result = sar::control::decode_preset_file(empty);
  assert(!empty_result.ok());
  assert(empty_result.error().code == "invalid_preset_file");

  auto truncated = encoded.bytes();
  truncated.pop_back();
  const auto truncated_result = sar::control::decode_preset_file(truncated);
  assert(!truncated_result.ok());
  assert(truncated_result.error().code == "invalid_preset_file");

  const auto accepted = sar::control::command_accepted("not-a-preset-file");
  const auto encoded_state = sar::control::encode_control_response(accepted);
  assert(encoded_state.ok());
  const auto wrong_payload =
      sar::control::decode_preset_file(encoded_state.bytes);
  assert(!wrong_payload.ok());

  auto invalid_preset = preset;
  invalid_preset.sample_rate = 0;
  const auto invalid_encoded =
      sar::control::encode_preset_file(invalid_preset);
  assert(!invalid_encoded.ok());
  assert(invalid_encoded.error().code == "invalid_preset");
}
