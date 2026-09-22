#include "core/platform/windows_current_user_sid.h"

#include <cassert>
#include <string>

int main() {
  std::wstring sid;
  const bool resolved = sar::platform::current_user_sid_string(sid);
  // The CI runner and any interactive developer session both run this
  // process under a real user token, so resolution should succeed; a
  // hypothetical failure (no token, or a future non-Windows build of this
  // file) still leaves default_control_pipe_name() with a defined fallback,
  // checked below regardless of which branch this took.
  if (resolved) {
    assert(!sid.empty());
    assert(sid.starts_with(L"S-"));
  }

  const auto first = sar::platform::default_control_pipe_name();
  const auto second = sar::platform::default_control_pipe_name();
  assert(!first.empty());
  assert(first == second);  // Every caller in one user session must agree.
  assert(first.starts_with(L"sys-audio-route-control"));
  if (resolved) {
    assert(first == L"sys-audio-route-control-" + sid);
  } else {
    assert(first == L"sys-audio-route-control");
  }
  return 0;
}
