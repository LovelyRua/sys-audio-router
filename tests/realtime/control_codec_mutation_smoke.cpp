#include "core/control/control_response.h"
#include "core/control/control_wire_protocol.h"
#include "core/control/preset_file_codec.h"
#include "core/control/session_file_codec.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <vector>

// Deterministic mutation fuzzing of every decoder that parses bytes from
// outside the process (named-pipe protocol, session and preset files). The
// decoders must reject or accept any input without crashing or hanging.

namespace {

class Xorshift {
 public:
  explicit Xorshift(std::uint64_t seed) noexcept : state_(seed) {}
  std::uint64_t next() noexcept {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 7U;
    state_ ^= state_ << 17U;
    return state_;
  }
  std::size_t below(std::size_t bound) noexcept {
    return bound == 0 ? 0 : static_cast<std::size_t>(next() % bound);
  }

 private:
  std::uint64_t state_;
};

sar::control::PresetDocument make_preset() {
  sar::control::PresetDocument preset;
  preset.sample_rate = 48000;
  preset.frames_per_block = 128;
  preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  preset.matrix.inputs.push_back({"input-1", "Input 1"});
  preset.matrix.inputs.push_back({"input-2", "Input 2"});
  preset.matrix.outputs.push_back({"output-1", "Output 1"});
  preset.matrix.outputs.push_back({"output-2", "Output 2"});
  preset.matrix.routes.push_back({"input-1", "output-1", 0.5F, false});
  preset.matrix.routes.push_back({"input-2", "output-2", 1.0F, true});
  return preset;
}

std::vector<std::uint8_t> mutate(const std::vector<std::uint8_t>& original,
                                 Xorshift& random) {
  auto bytes = original;
  switch (random.below(6)) {
    case 0:
      if (!bytes.empty()) {
        bytes[random.below(bytes.size())] ^=
            static_cast<std::uint8_t>(1U << random.below(8));
      }
      break;
    case 1:
      if (!bytes.empty()) {
        bytes[random.below(bytes.size())] =
            static_cast<std::uint8_t>(random.next());
      }
      break;
    case 2: {
      static constexpr std::uint32_t kInteresting[] = {
          0U, 1U, 0x7FFFFFFFU, 0x80000000U, 0xFFFFFFFFU, 0x0000FFFFU};
      if (bytes.size() >= 4) {
        const auto offset = random.below(bytes.size() - 3);
        const auto value = kInteresting[random.below(6)];
        for (std::size_t index = 0; index < 4; ++index) {
          bytes[offset + index] =
              static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
        }
      }
      break;
    }
    case 3:
      bytes.resize(random.below(bytes.size() + 1));
      break;
    case 4: {
      const auto extra = 1 + random.below(16);
      for (std::size_t index = 0; index < extra; ++index) {
        bytes.push_back(static_cast<std::uint8_t>(random.next()));
      }
      break;
    }
    default: {
      const auto edits = 1 + random.below(8);
      for (std::size_t index = 0; index < edits && !bytes.empty(); ++index) {
        bytes[random.below(bytes.size())] =
            static_cast<std::uint8_t>(random.next());
      }
      break;
    }
  }
  return bytes;
}

// Returns how many mutated inputs the decoder rejected.
std::size_t fuzz(const char* name,
                 const std::vector<std::uint8_t>& valid,
                 std::uint64_t seed,
                 std::size_t iterations,
                 const std::function<bool(const std::vector<std::uint8_t>&)>&
                     decode_ok) {
  std::fprintf(stderr, "fuzzing %s\n", name);
  assert(decode_ok(valid));
  Xorshift random(seed);
  std::size_t rejected = 0;
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    const auto input = mutate(valid, random);
    try {
      if (!decode_ok(input)) {
        ++rejected;
      }
    } catch (const std::exception& error) {
      std::fprintf(stderr, "%s: iteration %zu threw: %s\n", name,
                   iteration, error.what());
      std::fprintf(stderr, "input (%zu bytes):", input.size());
      for (std::size_t index = 0; index < input.size() && index < 96; ++index) {
        std::fprintf(stderr, " %02x", input[index]);
      }
      std::fprintf(stderr, "\n");
      std::abort();
    }
  }
  return rejected;
}

}  // namespace

int main() {
  constexpr std::size_t kIterations = 30000;

  sar::control::ControlCommand command;
  command.command_id = "fuzz-1";
  command.type = sar::control::ControlCommandType::SetGain;
  command.input_id = "input-1";
  command.output_id = "output-1";
  command.gain = 0.25F;
  command.preset = make_preset();
  command.audio_runtime.mode = sar::control::AudioRuntimeMode::WasapiMatrix;
  command.audio_runtime.endpoints = {
      {"capture-1", "native-capture-1",
       sar::control::AudioRuntimeEndpointDirection::Capture, false, 0, 2},
      {"render-1", "native-render-1",
       sar::control::AudioRuntimeEndpointDirection::Render, true, 0, 2},
  };
  command.virtual_asio_devices = {{
      .device_id = "main",
      .clsid = "{7F16C8A9-4A0C-4D31-9A5B-2C6E7F8D1042}",
      .registry_name = "System Audio Route Main",
      .broker_token = "main",
      .input_channels = 8,
      .output_channels = 8,
      .enabled = true,
  }};
  const auto command_bytes = sar::control::encode_control_command(command);
  assert(command_bytes.ok());
  assert(fuzz("command", command_bytes.bytes, 0x5AF1U, kIterations,
              [](const std::vector<std::uint8_t>& bytes) {
                const auto decoded = sar::control::decode_control_command(bytes);
                if (decoded.ok()) {
                  static_cast<void>(
                      sar::control::encode_control_command(decoded.command));
                }
                return decoded.ok();
              }) > 0);

  auto response = sar::control::command_accepted("fuzz-1");
  response.has_virtual_asio_devices = true;
  response.virtual_asio_devices = command.virtual_asio_devices;
  const auto response_bytes = sar::control::encode_control_response(response);
  assert(response_bytes.ok());
  assert(fuzz("response", response_bytes.bytes, 0xC0DEU, kIterations,
              [](const std::vector<std::uint8_t>& bytes) {
                return sar::control::decode_control_response(bytes).ok();
              }) > 0);

  const auto preset_bytes = sar::control::encode_preset_file(make_preset());
  assert(preset_bytes.ok());
  assert(fuzz("preset", preset_bytes.bytes(), 0xBEEFU, kIterations,
              [](const std::vector<std::uint8_t>& bytes) {
                return sar::control::decode_preset_file(bytes).ok();
              }) > 0);

  sar::control::SessionDocument session;
  session.preset.sample_rate = 48000;
  session.preset.frames_per_block = 128;
  session.preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  session.preset.matrix.inputs = {
      {"wasapi-capture-l", "WASAPI Capture L"},
      {"wasapi-capture-r", "WASAPI Capture R"},
      {"asio-output-l", "ASIO DAW Out 1"},
      {"asio-output-r", "ASIO DAW Out 2"},
  };
  session.preset.matrix.outputs = {
      {"wasapi-render-l", "WASAPI Render L"},
      {"wasapi-render-r", "WASAPI Render R"},
      {"asio-input-l", "ASIO DAW In 1"},
      {"asio-input-r", "ASIO DAW In 2"},
  };
  session.virtual_asio_devices.push_back({
      .device_id = "fuzz",
      .clsid = "{705B4C39-BEB4-47E7-8FA9-B61F1C901A20}",
      .registry_name = "SAR Fuzz Test",
      .broker_token = "fuzz",
      .input_channels = 2,
      .output_channels = 2,
  });
  const auto session_bytes = sar::control::encode_session_file(session);
  assert(session_bytes.ok());
  assert(fuzz("session", session_bytes.bytes(), 0xFACEU, kIterations,
              [](const std::vector<std::uint8_t>& bytes) {
                return sar::control::decode_session_file(bytes).ok();
              }) > 0);
  return 0;
}
