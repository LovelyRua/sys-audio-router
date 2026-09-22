#include "app/gui_vstgui/engine_client.h"

#include "core/control/control_wire_protocol.h"
#include "core/platform/windows_current_user_sid.h"
#include "core/service/windows_named_pipe_control.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <algorithm>
#include <optional>
#include <span>
#include <vector>

namespace sar::gui_vstgui {
namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& input) {
  std::vector<std::byte> result(input.size());
  std::transform(input.begin(), input.end(), result.begin(),
                 [](std::uint8_t value) { return static_cast<std::byte>(value); });
  return result;
}

std::vector<std::uint8_t> as_u8(std::span<const std::byte> input) {
  std::vector<std::uint8_t> result(input.size());
  std::transform(input.begin(), input.end(), result.begin(),
                 [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
  return result;
}

// Runs one command/response round trip. Populates EngineState::transportOk
// and, on failure, lastError; returns the decoded response only when the
// transport and decode both succeeded.
std::optional<control::ControlResponse> transact(
    const sar::service::NamedPipeControlConfig& config,
    control::ControlCommand command,
    EngineState& state) {
  const auto encoded = control::encode_control_command(command);
  if (!encoded.ok()) {
    state.transportOk = false;
    state.lastError = "Could not encode the control request";
    return std::nullopt;
  }
  const auto transaction = sar::service::transact_named_pipe_control(
      config, as_bytes(encoded.bytes), 2000);
  if (!transaction.ok()) {
    state.transportOk = false;
    state.lastError = transaction.error().message;
    return std::nullopt;
  }
  const auto decoded = control::decode_control_response(as_u8(transaction.payload()));
  if (!decoded.ok()) {
    state.transportOk = false;
    state.lastError = "The engine response could not be decoded";
    return std::nullopt;
  }
  state.transportOk = true;
  return decoded.response;
}

}  // namespace

EngineClient::EngineClient()
    : pipe_name_(sar::platform::default_control_pipe_name()),
      command_prefix_("gui-vstgui-" + std::to_string(GetCurrentProcessId()) + "-") {}

std::string EngineClient::next_command_id() {
  return command_prefix_ +
        std::to_string(command_sequence_.fetch_add(1, std::memory_order_relaxed));
}

EngineState EngineClient::poll() {
  EngineState state;
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = pipe_name_;

  control::ControlCommand runtime_query;
  runtime_query.command_id = next_command_id();
  runtime_query.type = control::ControlCommandType::QueryAudioRuntime;
  const auto runtime_response = transact(config, std::move(runtime_query), state);
  if (!runtime_response) {
    return state;
  }
  if (runtime_response->has_audio_runtime_state) {
    state.runtimeConfigured = runtime_response->audio_runtime.configured;
    state.runtimeRunning = runtime_response->audio_runtime.running;
  }

  control::ControlCommand session_query;
  session_query.command_id = next_command_id();
  session_query.type = control::ControlCommandType::QuerySessionState;
  const auto session_response = transact(config, std::move(session_query), state);
  if (session_response && session_response->has_active_graph) {
    state.sampleRate = session_response->active_graph.sample_rate;
    state.blockFrames =
        static_cast<std::uint32_t>(session_response->active_graph.frames);
  }

  control::ControlCommand diagnostics_query;
  diagnostics_query.command_id = next_command_id();
  diagnostics_query.type = control::ControlCommandType::QueryDiagnostics;
  const auto diagnostics_response = transact(config, std::move(diagnostics_query), state);
  if (diagnostics_response && diagnostics_response->has_diagnostics) {
    state.xrunCount = diagnostics_response->diagnostics.xrun_count;
  }

  return state;
}

EngineState EngineClient::start() {
  EngineState state;
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = pipe_name_;
  control::ControlCommand command;
  command.command_id = next_command_id();
  command.type = control::ControlCommandType::StartAudioRuntime;
  transact(config, std::move(command), state);
  return poll();
}

EngineState EngineClient::stop() {
  EngineState state;
  sar::service::NamedPipeControlConfig config;
  config.pipe_name = pipe_name_;
  control::ControlCommand command;
  command.command_id = next_command_id();
  command.type = control::ControlCommandType::StopAudioRuntime;
  transact(config, std::move(command), state);
  return poll();
}

}  // namespace sar::gui_vstgui
