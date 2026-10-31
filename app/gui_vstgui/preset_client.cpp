#include "app/gui_vstgui/preset_client.h"
#include "app/gui_vstgui/command_ids.h"
#include "core/control/preset_file_codec.h"
#include "core/control/control_wire_protocol.h"
#include "core/platform/windows_current_user_sid.h"
#include "core/service/windows_named_pipe_control.h"

#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace sar::gui_vstgui {
namespace {
namespace fs = std::filesystem;
constexpr auto kSuffix = L".sarpreset";
std::atomic_flag working = ATOMIC_FLAG_INIT;

std::wstring wide(const std::string& value) {
  if (value.empty() || value.size() > 320) return {};
  const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (!count) return {};
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), count);
  return result;
}
std::string utf8(const std::wstring& value) {
  const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!count) return {};
  std::string result(count, '\0');
  WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
      static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
  return result;
}
bool space(wchar_t c) {
  return c == L' ' || (c >= 9 && c <= 13) || c == 0x85 || c == 0xa0 ||
      c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 ||
      c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000;
}
fs::path directory() {
  wchar_t roaming[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, roaming)))
    throw std::runtime_error("Could not locate roaming application data.");
  // Matches Qt AppDataLocation with both organization and application names.
  return fs::path(roaming) / L"System Audio Route" / L"System Audio Route" / L"presets";
}
void checkDirectory(const fs::path& path) {
  // Never traverse a redirected preset directory (including its app parents).
  auto current = path.root_path();
  for (const auto& part : path.relative_path()) {
    current /= part;
    const auto attr = GetFileAttributesW(current.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) {
      const auto error = GetLastError();
      if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return;
      throw std::runtime_error("Cannot inspect the preset directory.");
    }
    if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) || !(attr & FILE_ATTRIBUTE_DIRECTORY))
      throw std::runtime_error("Preset directory must be a regular, non-redirected directory.");
  }
}
bool regularFileExists(const fs::path& path) {
  const auto attr = GetFileAttributesW(path.c_str());
  if (attr == INVALID_FILE_ATTRIBUTES) {
    const auto error = GetLastError();
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
    throw std::runtime_error("Cannot inspect the preset file.");
  }
  if (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
    throw std::runtime_error("Preset must be a regular, non-redirected file.");
  return true;
}
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
control::PresetDocument read(const fs::path& path) {
  Handle file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
      OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
  BY_HANDLE_FILE_INFORMATION info{};
  if (file.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.value, &info) ||
      (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
    throw std::runtime_error("Could not open a regular preset file.");
  if (info.nFileSizeHigh || info.nFileSizeLow > control::kControlWireMaxMessageBytes)
    throw std::runtime_error("Preset exceeds the 1 MiB size limit.");
  std::vector<std::uint8_t> bytes(info.nFileSizeLow);
  DWORD received = 0;
  if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &received, nullptr) ||
      received != bytes.size()) throw std::runtime_error("Could not read the complete preset.");
  auto decoded = control::decode_preset_file(bytes);
  if (!decoded.ok()) throw std::runtime_error(decoded.error().message);
  return decoded.take_preset();
}
void write(const fs::path& path, const control::PresetDocument& preset, bool replace) {
  const auto encoded = control::encode_preset_file(preset);
  if (!encoded.ok()) throw std::runtime_error(encoded.error().message);
  fs::create_directories(path.parent_path());
  checkDirectory(path.parent_path());
  wchar_t temporary[MAX_PATH]{};
  if (!GetTempFileNameW(path.parent_path().c_str(), L"sar", 0, temporary))
    throw std::runtime_error("Could not create a temporary preset file.");
  struct Cleanup { const wchar_t* path; ~Cleanup() { DeleteFileW(path); } } cleanup{temporary};
  {
    Handle file{CreateFileW(temporary, GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    DWORD written = 0;
    const auto& bytes = encoded.bytes();
    if (file.value == INVALID_HANDLE_VALUE ||
        !WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
        written != bytes.size() || !FlushFileBuffers(file.value))
      throw std::runtime_error("Could not write the preset. Existing file retained.");
  }
  (void)regularFileExists(path);
  if (!MoveFileExW(temporary, path.c_str(), MOVEFILE_WRITE_THROUGH |
      (replace ? MOVEFILE_REPLACE_EXISTING : 0)))
    throw std::runtime_error("Could not commit preset. It may already exist; refresh and try again.");
}
control::ControlResponse transact(control::ControlCommand command) {
  GuiCommandIds ids;
  command.command_id = ids.next();
  const auto encoded = control::encode_control_command(command);
  if (!encoded.ok()) throw std::runtime_error("Could not encode preset command.");
  service::NamedPipeControlConfig config;
  config.pipe_name = platform::default_control_pipe_name();
  const auto transaction = service::transact_named_pipe_control(config,
      std::as_bytes(std::span(encoded.bytes)), 2000);
  if (!transaction.ok()) throw std::runtime_error(transaction.error().message +
      " Command outcome may be unknown; no automatic retry was made.");
  std::vector<std::uint8_t> bytes(transaction.payload().size());
  std::transform(transaction.payload().begin(), transaction.payload().end(), bytes.begin(),
      [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
  auto decoded = control::decode_control_response(bytes);
  if (!decoded.ok() || decoded.response.command_id != command.command_id)
    throw std::runtime_error("Invalid or mismatched engine reply; command outcome is unknown.");
  if (decoded.response.status != control::ControlResponseStatus::Accepted)
    throw std::runtime_error(decoded.response.errors.empty() ? "Engine rejected preset command."
        : decoded.response.errors.front().message);
  return std::move(decoded.response);
}
std::vector<std::string> list(const fs::path& path) {
  std::vector<std::string> names;
  if (!fs::exists(path)) return names;
  std::size_t inspected = 0;
  for (const auto& entry : fs::directory_iterator(path)) {
    if (++inspected > 16384 || names.size() >= 4096)
      throw std::runtime_error("Preset directory exceeds listing limit (4096 presets / 16384 entries).");
    if (_wcsicmp(entry.path().extension().c_str(), kSuffix) != 0) continue;
    const auto name = utf8(entry.path().stem().wstring());
    if (validPresetName(name) && regularFileExists(entry.path())) names.push_back(name);
  }
  std::sort(names.begin(), names.end(), [](const auto& a, const auto& b) {
    return CompareStringOrdinal(wide(a).c_str(), -1, wide(b).c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  return names;
}
}  // namespace

bool validPresetName(const std::string& name) {
  const auto value = wide(name);
  if (value.empty() || value.size() > 80 || space(value.front()) || space(value.back()) ||
      value.back() == L'.') return false;
  for (const auto c : value)
    if (c < 32 || std::wstring_view(L"<>:\"/\\|?*").find(c) != std::wstring_view::npos) return false;
  const auto stem = value.substr(0, value.find(L'.'));
  for (const auto reserved : {L"CON", L"PRN", L"AUX", L"NUL", L"CONIN$", L"CONOUT$"})
    if (_wcsicmp(stem.c_str(), reserved) == 0) return false;
  if (stem.size() == 4 && (_wcsnicmp(stem.c_str(), L"COM", 3) == 0 ||
      _wcsnicmp(stem.c_str(), L"LPT", 3) == 0) &&
      ((stem[3] >= L'1' && stem[3] <= L'9') || stem[3] == 0xb9 || stem[3] == 0xb2 || stem[3] == 0xb3)) return false;
  return true;
}

PresetResult runPresetAction(PresetAction action, const std::string& name, bool overwriteConfirmed) {
  PresetResult result;
  if (working.test_and_set()) { result.message = "Another preset request is still finishing. Try again."; return result; }
  struct Release { ~Release() { working.clear(); } } release;
  try {
    if (action != PresetAction::List && !validPresetName(name))
      throw std::runtime_error("Use a name of 1-80 characters without path characters or reserved device names.");
    const auto root = directory();
    checkDirectory(root);
    const auto path = root / (wide(name) + kSuffix);
    if (action == PresetAction::Save) {
      if (regularFileExists(path) && !overwriteConfirmed) {
        result.needsOverwrite = true;
        result.message = "Preset already exists.";
        return result;
      }
      control::ControlCommand command;
      command.type = control::ControlCommandType::SavePreset;
      const auto response = transact(std::move(command));
      if (!response.has_preset) throw std::runtime_error("Engine did not return a preset document.");
      write(path, response.preset, overwriteConfirmed);
      result.message = "Saved preset: " + name;
    } else if (action == PresetAction::Load) {
      control::ControlCommand command;
      command.type = control::ControlCommandType::LoadPreset;
      command.preset = read(path);
      (void)transact(std::move(command));
      result.message = "Engine accepted preset: " + name;
    } else if (action == PresetAction::Delete) {
      if (!regularFileExists(path) || !DeleteFileW(path.c_str())) throw std::runtime_error("Could not delete preset.");
      result.message = "Deleted preset: " + name;
    }
    result.succeeded = true;
    try {
      result.names = list(root);
      result.hasList = true;
      if (action == PresetAction::List) result.message = std::to_string(result.names.size()) + " saved presets";
    } catch (const std::exception& error) {
      result.message += std::string(" Refresh failed: ") + error.what();
      if (action == PresetAction::List) result.succeeded = false;
    }
  } catch (const std::exception& error) { result.message = error.what(); }
  catch (...) { result.message = "Preset request failed unexpectedly. No automatic retry was made."; }
  return result;
}
}  // namespace sar::gui_vstgui
