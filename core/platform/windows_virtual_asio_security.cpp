#include "core/platform/windows_virtual_asio_security.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <Aclapi.h>

#include <cstddef>
#include <new>
#include <utility>

namespace sar::platform {
namespace {

class TokenHandle {
 public:
  TokenHandle() = default;
  TokenHandle(const TokenHandle&) = delete;
  TokenHandle& operator=(const TokenHandle&) = delete;
  ~TokenHandle() {
    if (handle_ != nullptr) {
      CloseHandle(handle_);
    }
  }

  HANDLE* receive() noexcept { return &handle_; }
  HANDLE get() const noexcept { return handle_; }

 private:
  HANDLE handle_ = nullptr;
};

WindowsVirtualAsioSecurityResult failure(std::string code,
                                         std::string message,
                                         DWORD native_error = 0) {
  return WindowsVirtualAsioSecurityResult::failure({
      {std::move(code), std::move(message), native_error},
  });
}

}  // namespace

struct WindowsVirtualAsioSecurityAttributes::Impl {
  ~Impl() {
    if (acl != nullptr) {
      LocalFree(acl);
    }
  }

  std::vector<std::byte> token_user;
  std::vector<std::byte> system_sid;
  PACL acl = nullptr;
  SECURITY_DESCRIPTOR descriptor{};
  SECURITY_ATTRIBUTES attributes{};
};

WindowsVirtualAsioSecurityResult WindowsVirtualAsioSecurityResult::success(
    std::unique_ptr<WindowsVirtualAsioSecurityAttributes> attributes) {
  return {std::move(attributes), {}};
}

WindowsVirtualAsioSecurityResult WindowsVirtualAsioSecurityResult::failure(
    std::vector<WindowsVirtualAsioSecurityError> errors) {
  return {nullptr, std::move(errors)};
}

bool WindowsVirtualAsioSecurityResult::ok() const noexcept {
  return attributes_ != nullptr && errors_.empty();
}

WindowsVirtualAsioSecurityAttributes&
WindowsVirtualAsioSecurityResult::attributes() noexcept {
  return *attributes_;
}

const WindowsVirtualAsioSecurityAttributes&
WindowsVirtualAsioSecurityResult::attributes() const noexcept {
  return *attributes_;
}

std::unique_ptr<WindowsVirtualAsioSecurityAttributes>
WindowsVirtualAsioSecurityResult::take_attributes() noexcept {
  return std::move(attributes_);
}

const std::vector<WindowsVirtualAsioSecurityError>&
WindowsVirtualAsioSecurityResult::errors() const noexcept {
  return errors_;
}

WindowsVirtualAsioSecurityResult::WindowsVirtualAsioSecurityResult(
    std::unique_ptr<WindowsVirtualAsioSecurityAttributes> attributes,
    std::vector<WindowsVirtualAsioSecurityError> errors)
    : attributes_(std::move(attributes)), errors_(std::move(errors)) {}

WindowsVirtualAsioSecurityAttributes::WindowsVirtualAsioSecurityAttributes(
    WindowsVirtualAsioSecurityAttributes&& other) noexcept = default;

WindowsVirtualAsioSecurityAttributes&
WindowsVirtualAsioSecurityAttributes::operator=(
    WindowsVirtualAsioSecurityAttributes&& other) noexcept = default;

WindowsVirtualAsioSecurityAttributes::~WindowsVirtualAsioSecurityAttributes() =
    default;

WindowsVirtualAsioSecurityResult
WindowsVirtualAsioSecurityAttributes::create_for_current_user() {
  try {
    auto impl = std::make_unique<Impl>();

    TokenHandle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, token.receive())) {
      return failure("virtual_asio_security_token_open_failed",
                     "Could not open the current process token.",
                     GetLastError());
    }

    DWORD token_user_bytes = 0;
    if (GetTokenInformation(token.get(),
                            TokenUser,
                            nullptr,
                            0,
                            &token_user_bytes)) {
      return failure("virtual_asio_security_token_user_size_failed",
                     "Could not determine the current token user SID size.",
                     ERROR_INVALID_DATA);
    }
    const DWORD token_user_size_error = GetLastError();
    if (token_user_size_error != ERROR_INSUFFICIENT_BUFFER ||
        token_user_bytes < sizeof(TOKEN_USER)) {
      return failure("virtual_asio_security_token_user_size_failed",
                     "Could not determine the current token user SID size.",
                     token_user_size_error);
    }
    impl->token_user.resize(token_user_bytes);
    if (!GetTokenInformation(token.get(),
                             TokenUser,
                             impl->token_user.data(),
                             token_user_bytes,
                             &token_user_bytes)) {
      return failure("virtual_asio_security_token_user_read_failed",
                     "Could not read the current token user SID.",
                     GetLastError());
    }
    auto* token_user =
        reinterpret_cast<TOKEN_USER*>(impl->token_user.data());
    if (!IsValidSid(token_user->User.Sid)) {
      return failure("virtual_asio_security_token_user_sid_invalid",
                     "The current token returned an invalid user SID.");
    }

    impl->system_sid.resize(SECURITY_MAX_SID_SIZE);
    DWORD system_sid_bytes = static_cast<DWORD>(impl->system_sid.size());
    if (!CreateWellKnownSid(WinLocalSystemSid,
                            nullptr,
                            impl->system_sid.data(),
                            &system_sid_bytes)) {
      return failure("virtual_asio_security_system_sid_create_failed",
                     "Could not construct the LocalSystem SID.",
                     GetLastError());
    }

    EXPLICIT_ACCESSW entries[2]{};
    entries[0].grfAccessPermissions = GENERIC_ALL;
    entries[0].grfAccessMode = SET_ACCESS;
    entries[0].grfInheritance = NO_INHERITANCE;
    entries[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[0].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[0].Trustee.ptstrName =
        static_cast<LPWSTR>(token_user->User.Sid);

    entries[1].grfAccessPermissions = GENERIC_ALL;
    entries[1].grfAccessMode = SET_ACCESS;
    entries[1].grfInheritance = NO_INHERITANCE;
    entries[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entries[1].Trustee.TrusteeType = TRUSTEE_IS_USER;
    entries[1].Trustee.ptstrName =
        reinterpret_cast<LPWSTR>(impl->system_sid.data());

    const DWORD acl_error =
        SetEntriesInAclW(2, entries, nullptr, &impl->acl);
    if (acl_error != ERROR_SUCCESS) {
      return failure("virtual_asio_security_acl_create_failed",
                     "Could not create the Virtual ASIO access control list.",
                     acl_error);
    }
    if (!InitializeSecurityDescriptor(&impl->descriptor,
                                      SECURITY_DESCRIPTOR_REVISION)) {
      return failure("virtual_asio_security_descriptor_init_failed",
                     "Could not initialize the Virtual ASIO security descriptor.",
                     GetLastError());
    }
    if (!SetSecurityDescriptorDacl(&impl->descriptor,
                                   TRUE,
                                   impl->acl,
                                   FALSE)) {
      return failure("virtual_asio_security_descriptor_dacl_failed",
                     "Could not attach the Virtual ASIO access control list.",
                     GetLastError());
    }

    impl->attributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    impl->attributes.lpSecurityDescriptor = &impl->descriptor;
    impl->attributes.bInheritHandle = FALSE;
    return WindowsVirtualAsioSecurityResult::success(
        std::unique_ptr<WindowsVirtualAsioSecurityAttributes>(
            new WindowsVirtualAsioSecurityAttributes(std::move(impl))));
  } catch (const std::bad_alloc&) {
    return failure("virtual_asio_security_allocation_failed",
                   "Could not allocate Virtual ASIO security state.");
  }
}

bool WindowsVirtualAsioSecurityAttributes::valid() const noexcept {
  return impl_ != nullptr && impl_->acl != nullptr &&
         impl_->attributes.lpSecurityDescriptor == &impl_->descriptor;
}

void* WindowsVirtualAsioSecurityAttributes::native_attributes() noexcept {
  return valid() ? &impl_->attributes : nullptr;
}

const void* WindowsVirtualAsioSecurityAttributes::native_attributes()
    const noexcept {
  return valid() ? &impl_->attributes : nullptr;
}

WindowsVirtualAsioSecurityAttributes::WindowsVirtualAsioSecurityAttributes(
    std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

}  // namespace sar::platform
