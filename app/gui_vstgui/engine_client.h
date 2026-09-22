#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace sar::gui_vstgui {

// A snapshot of the state the header bar displays. Deliberately small: this
// is the first slice of the VSTGUI control panel, covering only what the
// header needs. Later slices (routing matrix, devices, diagnostics detail)
// add their own query/state types the same way rather than growing this one
// into a god object.
struct EngineState {
  bool transportOk = false;
  bool runtimeConfigured = false;
  bool runtimeRunning = false;
  std::uint32_t sampleRate = 0;
  std::uint32_t blockFrames = 0;
  std::uint64_t xrunCount = 0;
  std::string lastError;
};

// Talks to the engine over the same per-user named pipe the engine service,
// sar_control_cli, and the Qt control panel use
// (core/platform/windows_current_user_sid.h). Every method here blocks on
// pipe I/O and must be called off the UI thread; callers marshal the
// resulting EngineState back to the UI thread themselves (see
// Async::schedule in main_window.cpp) so this class stays free of any UI
// toolkit dependency.
class EngineClient final {
 public:
  EngineClient();

  // Sends QueryAudioRuntime and, if the runtime is configured, QueryDiagnostics
  // to refresh the counters. Never throws; a transport failure is reported
  // through EngineState::transportOk instead.
  [[nodiscard]] EngineState poll();

  // Fire-and-wait StartAudioRuntime / StopAudioRuntime. Returns the state
  // observed right after, same as poll().
  [[nodiscard]] EngineState start();
  [[nodiscard]] EngineState stop();

 private:
  [[nodiscard]] std::string next_command_id();

  std::wstring pipe_name_;
  std::string command_prefix_;
  std::atomic<std::uint64_t> command_sequence_{0};
};

}  // namespace sar::gui_vstgui
