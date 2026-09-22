#include "core/platform/windows_current_user_sid.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <sddl.h>

#include <cstddef>
#include <vector>

namespace sar::platform {

bool current_user_sid_string(std::wstring& value) noexcept {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    return false;
  }
  DWORD required = 0;
  static_cast<void>(
      GetTokenInformation(token, TokenUser, nullptr, 0, &required));
  if (required == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    CloseHandle(token);
    return false;
  }
  std::vector<std::byte> storage(required);
  const bool read = GetTokenInformation(token, TokenUser, storage.data(),
                                        required, &required) != FALSE;
  CloseHandle(token);
  if (!read) {
    return false;
  }
  const auto* user = reinterpret_cast<const TOKEN_USER*>(storage.data());
  LPWSTR text = nullptr;
  if (!ConvertSidToStringSidW(user->User.Sid, &text) || text == nullptr) {
    return false;
  }
  value.assign(text);
  LocalFree(text);
  return true;
}

std::wstring default_control_pipe_name() {
  std::wstring sid;
  if (!current_user_sid_string(sid)) {
    return L"sys-audio-route-control";
  }
  return L"sys-audio-route-control-" + sid;
}

}  // namespace sar::platform
