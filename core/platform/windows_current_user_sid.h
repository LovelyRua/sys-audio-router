#pragma once

#include <string>

namespace sar::platform {

// Returns the current process token's user SID as a string (for example
// "S-1-5-21-...-1001"). Returns false if the SID could not be determined.
[[nodiscard]] bool current_user_sid_string(std::wstring& value) noexcept;

// A stable, per-user default name for the engine's named-pipe control
// surface: "sys-audio-route-control-<sid>". Every process that needs to
// agree on the engine's default pipe without an explicit --pipe argument
// (the engine service, the control CLI, the bootstrap launcher, and the GUI)
// calls this instead of hard-coding the name, so two different users on the
// same machine never contend for one pipe. Falls back to the plain
// "sys-audio-route-control" name if the current user's SID cannot be
// determined, so a single-user machine still works.
[[nodiscard]] std::wstring default_control_pipe_name();

}  // namespace sar::platform
