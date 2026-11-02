#pragma once

#include "core/platform/windows_current_user_sid.h"

#include <string>
#include <string_view>

namespace sar::platform {

// The engine, registered DLL and unregistered development DLL must derive
// identical broker names. Never drop the user SID when adding an ASIO token.
inline std::wstring virtual_asio_broker_pipe_name(
    std::wstring_view broker_token, std::wstring_view control_pipe = {}) {
  std::wstring result = control_pipe.empty()
                            ? default_control_pipe_name()
                            : std::wstring(control_pipe);
  result += L'-';
  result += broker_token;
  return result;
}

}  // namespace sar::platform
