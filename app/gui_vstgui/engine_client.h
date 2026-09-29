#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "core/control/preset_document.h"

namespace sar::gui_vstgui {

// One engine snapshot for the header and route matrix.
struct EngineState {
  bool transportOk = false;
  bool runtimeConfigured = false;
  bool runtimeRunning = false;
  std::uint32_t sampleRate = 0;
  std::uint32_t blockFrames = 0;
  std::uint64_t xrunCount = 0;
  std::string lastError;
  control::PresetRouteMatrix matrix;
  bool hasMatrix = false;
};

// Talks to the engine over the same per-user named pipe the engine service,
// sar_control_cli, and the Qt control panel use
// (core/platform/windows_current_user_sid.h). Every method here blocks on
// pipe I/O and must be called off the UI thread; callers marshal the
// resulting EngineState back to the UI thread themselves (see
// HeaderController in main_window.cpp) so this class stays free of any UI
// toolkit dependency.
class EngineClient final {
 public:
  explicit EngineClient(std::wstring pipe_name = {});

  // Queries runtime, session (including preset matrix), and diagnostics.
  // A transport failure is reported
  // through EngineState::transportOk instead.
  [[nodiscard]] EngineState poll();

  // Fire-and-wait StartAudioRuntime / StopAudioRuntime. Returns the state
  // observed right after, same as poll().
  [[nodiscard]] EngineState start();
  [[nodiscard]] EngineState stop();
  [[nodiscard]] EngineState setRoute(std::string input_id, std::string output_id,
                                     bool connect);
  [[nodiscard]] EngineState setRouteGain(std::string input_id, std::string output_id,
                                         float gain);
  [[nodiscard]] EngineState setRouteMuted(std::string input_id, std::string output_id,
                                          bool muted);

 private:
  [[nodiscard]] std::string next_command_id();

  std::wstring pipe_name_;
  std::string command_prefix_;
  std::atomic<std::uint64_t> command_sequence_{0};
};

}  // namespace sar::gui_vstgui
