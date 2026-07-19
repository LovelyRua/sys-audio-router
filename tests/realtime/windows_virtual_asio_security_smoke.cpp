#include "core/platform/windows_virtual_asio_security.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cassert>
#include <cstddef>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

class HandleScope {
 public:
  explicit HandleScope(HANDLE handle) noexcept : handle_(handle) {}
  HandleScope(const HandleScope&) = delete;
  HandleScope& operator=(const HandleScope&) = delete;
  ~HandleScope() {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
  }

 private:
  HANDLE handle_ = nullptr;
};

std::vector<std::byte> current_user_sid() {
  HANDLE raw_token = nullptr;
  assert(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw_token));
  HandleScope token(raw_token);

  DWORD bytes = 0;
  assert(!GetTokenInformation(raw_token, TokenUser, nullptr, 0, &bytes));
  assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
  std::vector<std::byte> storage(bytes);
  assert(GetTokenInformation(
      raw_token, TokenUser, storage.data(), bytes, &bytes));
  auto* user = reinterpret_cast<TOKEN_USER*>(storage.data());
  const DWORD sid_bytes = GetLengthSid(user->User.Sid);
  std::vector<std::byte> sid(sid_bytes);
  assert(CopySid(sid_bytes, sid.data(), user->User.Sid));
  return sid;
}

std::vector<std::byte> local_system_sid() {
  std::vector<std::byte> sid(SECURITY_MAX_SID_SIZE);
  DWORD bytes = static_cast<DWORD>(sid.size());
  assert(CreateWellKnownSid(WinLocalSystemSid, nullptr, sid.data(), &bytes));
  sid.resize(bytes);
  return sid;
}

std::wstring unique_name(const wchar_t* kind) {
  return L"Local\\SAR.VirtualASIO.v1.security." +
         std::to_wstring(GetCurrentProcessId()) + L"." +
         std::to_wstring(GetTickCount64()) + L"." + kind;
}

}  // namespace

int main() {
  using sar::platform::WindowsVirtualAsioSecurityAttributes;

  static_assert(!std::is_copy_constructible_v<
                WindowsVirtualAsioSecurityAttributes>);
  static_assert(!std::is_copy_assignable_v<
                WindowsVirtualAsioSecurityAttributes>);
  static_assert(std::is_nothrow_move_constructible_v<
                WindowsVirtualAsioSecurityAttributes>);
  static_assert(std::is_nothrow_move_assignable_v<
                WindowsVirtualAsioSecurityAttributes>);

  auto created = WindowsVirtualAsioSecurityAttributes::create_for_current_user();
  assert(created.ok());
  assert(created.errors().empty());
  auto security = created.take_attributes();
  assert(security != nullptr);
  assert(security->valid());

  auto* attributes =
      static_cast<SECURITY_ATTRIBUTES*>(security->native_attributes());
  assert(attributes != nullptr);
  assert(attributes->nLength == sizeof(SECURITY_ATTRIBUTES));
  assert(attributes->bInheritHandle == FALSE);
  assert(attributes->lpSecurityDescriptor != nullptr);
  assert(IsValidSecurityDescriptor(attributes->lpSecurityDescriptor));

  BOOL dacl_present = FALSE;
  BOOL dacl_defaulted = TRUE;
  PACL dacl = nullptr;
  assert(GetSecurityDescriptorDacl(attributes->lpSecurityDescriptor,
                                   &dacl_present,
                                   &dacl,
                                   &dacl_defaulted));
  assert(dacl_present == TRUE);
  assert(dacl_defaulted == FALSE);
  assert(dacl != nullptr);

  ACL_SIZE_INFORMATION acl_info{};
  assert(GetAclInformation(
      dacl, &acl_info, sizeof(acl_info), AclSizeInformation));
  assert(acl_info.AceCount == 2);

  const auto user_sid = current_user_sid();
  const auto system_sid = local_system_sid();
  bool found_user = false;
  bool found_system = false;
  for (DWORD index = 0; index < acl_info.AceCount; ++index) {
    void* raw_ace = nullptr;
    assert(GetAce(dacl, index, &raw_ace));
    auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
    assert(ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE);
    assert((ace->Header.AceFlags & INHERITED_ACE) == 0);
    assert(ace->Mask == GENERIC_ALL);
    auto* sid = reinterpret_cast<SID*>(&ace->SidStart);
    assert(IsValidSid(sid));
    if (EqualSid(sid, const_cast<std::byte*>(user_sid.data()))) {
      assert(!found_user);
      found_user = true;
    } else if (EqualSid(sid, const_cast<std::byte*>(system_sid.data()))) {
      assert(!found_system);
      found_system = true;
    } else {
      assert(false && "DACL grants access to an unexpected SID");
    }
  }
  assert(found_user);
  assert(found_system);

  const auto event_name = unique_name(L"event");
  HANDLE raw_event =
      CreateEventW(attributes, FALSE, FALSE, event_name.c_str());
  assert(raw_event != nullptr);
  HandleScope event(raw_event);
  DWORD handle_flags = HANDLE_FLAG_INHERIT;
  assert(GetHandleInformation(raw_event, &handle_flags));
  assert((handle_flags & HANDLE_FLAG_INHERIT) == 0);
  HANDLE opened_event = OpenEventW(EVENT_MODIFY_STATE | SYNCHRONIZE,
                                   FALSE,
                                   event_name.c_str());
  assert(opened_event != nullptr);
  HandleScope event_peer(opened_event);
  assert(SetEvent(opened_event));
  assert(WaitForSingleObject(raw_event, 0) == WAIT_OBJECT_0);

  const auto mapping_name = unique_name(L"mapping");
  HANDLE raw_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
                                          attributes,
                                          PAGE_READWRITE,
                                          0,
                                          4096,
                                          mapping_name.c_str());
  assert(raw_mapping != nullptr);
  HandleScope mapping(raw_mapping);
  handle_flags = HANDLE_FLAG_INHERIT;
  assert(GetHandleInformation(raw_mapping, &handle_flags));
  assert((handle_flags & HANDLE_FLAG_INHERIT) == 0);
  HANDLE opened_mapping =
      OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE,
                       FALSE,
                       mapping_name.c_str());
  assert(opened_mapping != nullptr);
  HandleScope mapping_peer(opened_mapping);

  WindowsVirtualAsioSecurityAttributes moved(std::move(*security));
  assert(moved.valid());
  assert(!security->valid());
  assert(security->native_attributes() == nullptr);
}
