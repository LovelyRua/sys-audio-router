#include "core/control/session_file_codec.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr wchar_t kClsid[] = L"{705B4C39-BEB4-47E7-8FA9-B61F1C901A10}";
constexpr wchar_t kRegistryName[] = L"SAR Lifecycle Test";

sar::control::SessionDocument initial_session() {
  sar::control::SessionDocument session;
  session.preset.sample_rate = 48000;
  session.preset.frames_per_block = 128;
  session.preset.nodes.push_back({"matrix", "Main Matrix", "route_matrix"});
  session.preset.matrix.inputs = {
      {"wasapi-capture-l", "WASAPI Capture L"},
      {"wasapi-capture-r", "WASAPI Capture R"},
      {"asio-output-l", "ASIO DAW Out 1"},
      {"asio-output-r", "ASIO DAW Out 2"},
  };
  session.preset.matrix.outputs = {
      {"wasapi-render-l", "WASAPI Render L"},
      {"wasapi-render-r", "WASAPI Render R"},
      {"asio-input-l", "ASIO DAW In 1"},
      {"asio-input-r", "ASIO DAW In 2"},
  };
  session.virtual_asio_devices.push_back({
      .device_id = "lifecycle",
      .clsid = "{705B4C39-BEB4-47E7-8FA9-B61F1C901A10}",
      .registry_name = "SAR Lifecycle Test",
      .broker_token = "lifecycle",
      .input_channels = 2,
      .output_channels = 2,
  });
  return session;
}

void write_file(const std::wstring& path, const std::vector<std::uint8_t>& data) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  assert(file != INVALID_HANDLE_VALUE);
  DWORD written = 0;
  assert(WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written,
                   nullptr));
  assert(written == data.size());
  CloseHandle(file);
}

std::string read_text(const std::wstring& path) {
  const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return {};
  LARGE_INTEGER size{};
  std::string text;
  if (GetFileSizeEx(file, &size) && size.QuadPart > 0) {
    text.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read,
                  nullptr)) {
      read = 0;
    }
    text.resize(read);
  }
  CloseHandle(file);
  return text;
}

bool wait_for_pipe(const std::wstring& name) {
  const auto path = L"\\\\.\\pipe\\" + name;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (std::chrono::steady_clock::now() < deadline) {
    if (WaitNamedPipeW(path.c_str(), 100)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

HANDLE start_process(const std::wstring& command_line) {
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
  PROCESS_INFORMATION process{};
  assert(CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process));
  CloseHandle(process.hThread);
  return process.hProcess;
}

// Runs the engine with stdout/stderr captured to a file, for failure reports.
std::string capture_engine_output(const std::wstring& command_line,
                                  const std::wstring& output_path,
                                  DWORD timeout_ms) {
  SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  const HANDLE output = CreateFileW(output_path.c_str(), GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &inherit, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
  if (output == INVALID_HANDLE_VALUE) return {};
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup{.cb = sizeof(STARTUPINFOW)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = output;
  startup.hStdError = output;
  PROCESS_INFORMATION process{};
  if (CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                     CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
    CloseHandle(process.hThread);
    if (WaitForSingleObject(process.hProcess, timeout_ms) != WAIT_OBJECT_0) {
      TerminateProcess(process.hProcess, 1);
    }
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    CloseHandle(output);
    return read_text(output_path) + "(exit code " + std::to_string(code) + ")";
  }
  CloseHandle(output);
  return "CreateProcess failed";
}

DWORD run_to_exit(const std::wstring& command_line, DWORD timeout_ms) {
  const HANDLE process = start_process(command_line);
  const auto waited = WaitForSingleObject(process, timeout_ms);
  assert(waited == WAIT_OBJECT_0);
  DWORD exit_code = 0xFFFFFFFF;
  assert(GetExitCodeProcess(process, &exit_code));
  CloseHandle(process);
  return exit_code;
}

void delete_registration() {
  const std::wstring clsid_path = std::wstring(L"Software\\Classes\\CLSID\\") + kClsid;
  const std::wstring asio_path = std::wstring(L"Software\\ASIO\\") + kRegistryName;
  static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, clsid_path.c_str()));
  static_cast<void>(RegDeleteTreeW(HKEY_CURRENT_USER, asio_path.c_str()));
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  assert(argc == 2);
  const std::wstring engine = L"\"" + std::wstring(argv[1]) + L"\"";
  delete_registration();

  const auto token = std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(GetTickCount64());
  const auto directory =
      std::filesystem::temp_directory_path() / (L"sar-lifecycle-" + token);
  std::filesystem::create_directories(directory);
  const auto session_path = (directory / L"session.sarsession").wstring();
  const auto log_path = (directory / L"logs" / L"engine.log").wstring();
  const auto encoded = sar::control::encode_session_file(initial_session());
  assert(encoded.ok());
  write_file(session_path, encoded.bytes());

  const std::wstring pipe = L"sar-lifecycle-control-" + token;
  const std::wstring common = L" --pipe " + pipe;

  // No engine is running yet: --stop reports "not running".
  assert(run_to_exit(engine + common + L" --stop", 10000) == 3);

  const HANDLE process = start_process(
      engine + common + L" --session \"" + session_path + L"\" --log-file \"" +
      log_path + L"\"");
  if (!wait_for_pipe(pipe)) {
    DWORD early_exit = 0;
    const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0 &&
                        GetExitCodeProcess(process, &early_exit);
    std::fprintf(stderr, "engine pipe never appeared; exited=%d code=%lu\n",
                 exited ? 1 : 0, static_cast<unsigned long>(early_exit));
    std::fprintf(stderr, "--- engine log ---\n%s\n---\n",
                 read_text(log_path).c_str());
    const auto direct_dir = directory / L"direct";
    std::filesystem::create_directories(direct_dir);
    std::fprintf(
        stderr, "--- engine without --log-file ---\n%s\n---\n",
        capture_engine_output(engine + L" --pipe " + pipe +
                                  L"-direct --session \"" + session_path + L"\"",
                              (direct_dir / L"out.txt").wstring(), 4000)
            .c_str());
    std::abort();
  }

  const auto opened = read_text(log_path);
  assert(opened.find("engine_service_log_opened") != std::string::npos);

  // A second engine on the same pipe must be refused and must not disturb
  // the first one.
  assert(run_to_exit(engine + common + L" --session \"" + session_path + L"\"",
                     10000) != 0);

  assert(run_to_exit(engine + common + L" --stop", 15000) == 0);
  assert(WaitForSingleObject(process, 10000) == WAIT_OBJECT_0);
  DWORD exit_code = 0xFFFFFFFF;
  assert(GetExitCodeProcess(process, &exit_code));
  assert(exit_code == 0);
  CloseHandle(process);

  const auto closed = read_text(log_path);
  assert(closed.find("engine_service_state=stopped") != std::string::npos);
  assert(run_to_exit(engine + common + L" --stop", 10000) == 3);

  delete_registration();
  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
  return 0;
}
