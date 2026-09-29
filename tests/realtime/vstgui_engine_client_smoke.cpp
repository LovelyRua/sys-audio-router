#include "app/gui_vstgui/engine_client.h"
#include "core/control/control_wire_protocol.h"
#include "core/service/windows_named_pipe_control.h"

#include <Windows.h>
#include <atomic>
#include <cassert>
#include <vector>

int main() {
  using namespace sar;
  service::NamedPipeControlConfig config;
  config.pipe_name = L"sar-vstgui-client-test-" + std::to_wstring(GetCurrentProcessId());
  std::atomic_bool reject_session{false};
  std::atomic_uint diagnostics_queries{0};
  service::WindowsNamedPipeControlServer server(config,
      [&](std::span<const std::byte> payload) {
        std::vector<std::uint8_t> bytes;
        for (auto value : payload) bytes.push_back(std::to_integer<std::uint8_t>(value));
        const auto decoded = control::decode_control_command(bytes);
        assert(decoded.ok());
        const auto& command = decoded.command;
        auto response = control::command_accepted(command.command_id);
        switch (command.type) {
          case control::ControlCommandType::StartAudioRuntime:
          case control::ControlCommandType::StopAudioRuntime:
            response = control::command_rejected(command.command_id,
                {{"test_failure", "Device could not start or stop"}});
            break;
          case control::ControlCommandType::QueryAudioRuntime:
            response.has_audio_runtime_state = true;
            response.audio_runtime.configured = true;
            break;
          case control::ControlCommandType::QuerySessionState:
            if (reject_session.load()) {
              response = control::command_rejected(command.command_id,
                  {{"session_failure", "Session query failed"}});
            } else {
              response.has_active_graph = true;
              response.active_graph.sample_rate = 48000;
              response.active_graph.frames = 128;
            }
            break;
          case control::ControlCommandType::QueryDiagnostics:
            ++diagnostics_queries;
            response.has_diagnostics = true;
            break;
          default: assert(false);
        }
        const auto encoded = control::encode_control_response(response);
        assert(encoded.ok());
        std::vector<std::byte> result;
        for (auto value : encoded.bytes) result.push_back(static_cast<std::byte>(value));
        return service::NamedPipeControlResult::success(std::move(result));
      });
  assert(server.start().ok());
  gui_vstgui::EngineClient client(config.pipe_name);
  const auto initial = client.poll();
  assert(initial.transportOk && initial.runtimeConfigured);
  assert(initial.sampleRate == 48000 && initial.blockFrames == 128);
  assert(initial.lastError.empty());
  assert(client.start().lastError == "Device could not start or stop");
  assert(client.stop().lastError == "Device could not start or stop");
  const auto count = diagnostics_queries.load();
  reject_session = true;
  assert(client.poll().lastError == "Session query failed");
  assert(diagnostics_queries.load() == count);
  server.stop();
  const auto offline = client.poll();
  assert(!offline.transportOk && !offline.lastError.empty());
}
