#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kControlPipe[] = L"\\\\.\\pipe\\sys-audio-route-control";
constexpr wchar_t kLauncherMutex[] =
    L"Local\\SystemAudioRoute.Launcher.{A53F02CB-86B7-4B63-A3EE-8C742498E60D}";

class UniqueHandle final {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(HANDLE handle) noexcept : handle_(handle) {}
  UniqueHandle(const UniqueHandle&) = delete;
  UniqueHandle& operator=(const UniqueHandle&) = delete;
  UniqueHandle(UniqueHandle&& other) noexcept
      : handle_(other.release()) {}
  UniqueHandle& operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }
  ~UniqueHandle() { reset(); }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }
  [[nodiscard]] HANDLE release() noexcept {
    const auto value = handle_;
    handle_ = nullptr;
    return value;
  }
  void reset(HANDLE handle = nullptr) noexcept {
    if (*this) {
      CloseHandle(handle_);
    }
    handle_ = handle;
  }

 private:
  HANDLE handle_ = nullptr;
};

class MutexLock final {
 public:
  MutexLock(const wchar_t* name, std::uint32_t timeout_ms) noexcept
      : handle_(CreateMutexW(nullptr, FALSE, name)) {
    if (!handle_) {
      return;
    }
    const auto result = WaitForSingleObject(handle_.get(), timeout_ms);
    owns_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
  }
  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;
  ~MutexLock() {
    if (owns_) {
      ReleaseMutex(handle_.get());
    }
  }

  [[nodiscard]] bool owns() const noexcept { return owns_; }

 private:
  UniqueHandle handle_;
  bool owns_ = false;
};

std::filesystem::path executable_directory() {
  std::vector<wchar_t> buffer(512);
  while (true) {
    const auto copied = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (copied == 0) {
      return {};
    }
    if (copied < buffer.size() - 1) {
      return std::filesystem::path(
                 std::wstring_view(buffer.data(), copied))
          .parent_path();
    }
    buffer.resize(buffer.size() * 2);
  }
}

std::wstring quoted(const std::filesystem::path& value) {
  return L"\"" + value.wstring() + L"\"";
}

std::filesystem::path engine_session_path() {
  std::vector<wchar_t> app_data(32768);
  const auto length = GetEnvironmentVariableW(
      L"APPDATA", app_data.data(),
      static_cast<DWORD>(app_data.size()));
  if (length == 0 || length >= app_data.size()) {
    return {};
  }
  return std::filesystem::path(
             std::wstring_view(app_data.data(), length)) /
         L"System Audio Route" / L"engine-session.sarsession";
}

bool pipe_ready() noexcept {
  if (WaitNamedPipeW(kControlPipe, 0) != FALSE) {
    return true;
  }
  const auto error = GetLastError();
  return error == ERROR_PIPE_BUSY || error == ERROR_SEM_TIMEOUT;
}

bool wait_for_engine(std::uint32_t timeout_ms) noexcept {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    if (pipe_ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
  }
  return pipe_ready();
}

bool launch_process(const std::filesystem::path& executable,
                    std::wstring arguments,
                    DWORD creation_flags,
                    PROCESS_INFORMATION& process) noexcept {
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  std::wstring command_line = quoted(executable);
  if (!arguments.empty()) {
    command_line += L" ";
    command_line += arguments;
  }
  return CreateProcessW(
             executable.c_str(), command_line.data(), nullptr, nullptr, FALSE,
             creation_flags, nullptr, executable.parent_path().c_str(),
             &startup, &process) != FALSE;
}

int fail(const wchar_t* message) noexcept {
  MessageBoxW(nullptr, message, L"System Audio Route",
              MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
  return 1;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  const auto directory = executable_directory();
  if (directory.empty()) {
    return fail(L"Could not locate the System Audio Route installation.");
  }

  const auto engine = directory / L"sar_engine_service.exe";
  const auto gui = directory / L"SystemAudioRoute.exe";
  if (!std::filesystem::is_regular_file(engine) ||
      !std::filesystem::is_regular_file(gui)) {
    return fail(L"The installation is incomplete. Reinstall System Audio Route.");
  }

  MutexLock launcher_lock(kLauncherMutex, 10000);
  if (!launcher_lock.owns()) {
    return fail(L"Another System Audio Route launch did not finish in time.");
  }

  UniqueHandle engine_process;
  UniqueHandle engine_thread;
  if (!pipe_ready()) {
    const auto session = engine_session_path();
    if (session.empty()) {
      return fail(L"Could not locate the local application data directory.");
    }
    std::error_code directory_error;
    std::filesystem::create_directories(session.parent_path(), directory_error);
    if (directory_error) {
      return fail(L"Could not create the engine session directory.");
    }

    PROCESS_INFORMATION launched{};
    if (!launch_process(engine, L"--session " + quoted(session),
                        DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP,
                        launched)) {
      return fail(L"Could not start the System Audio Route engine service.");
    }
    engine_process.reset(launched.hProcess);
    engine_thread.reset(launched.hThread);
    if (!wait_for_engine(5000)) {
      return fail(L"The engine service did not become ready within five seconds.");
    }
  }

  PROCESS_INFORMATION launched_gui{};
  if (!launch_process(gui, {}, 0, launched_gui)) {
    return fail(L"Could not start the System Audio Route control panel.");
  }
  UniqueHandle gui_process(launched_gui.hProcess);
  UniqueHandle gui_thread(launched_gui.hThread);
  return 0;
}
