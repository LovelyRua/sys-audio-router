#include "app/gui_vstgui/virtual_device_client.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace sar::gui_vstgui {
namespace {
constexpr std::size_t kMaximumOutput = 1024 * 1024;
constexpr DWORD kInspectTimeout = 15000;
constexpr DWORD kMutationTimeout = 90000;

struct Handle {
  HANDLE value = nullptr;
  Handle() = default;
  explicit Handle(HANDLE handle) : value(handle) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  void reset() {
    if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
    value = nullptr;
  }
};

struct Apartment {
  Apartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
  ~Apartment() { winrt::uninit_apartment(); }
};

struct MutationGate {
  std::mutex mutex;
  Handle pending;
  bool unknownWithoutHandle = false;
};

std::wstring wide(const std::string& value) {
  if (value.empty()) return {};
  if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
    throw std::runtime_error("UTF-8 input is too large");
  const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (!size) throw std::runtime_error("Invalid UTF-8");
  std::wstring result(size, L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), result.data(), size) != size)
    throw std::runtime_error("Invalid UTF-8");
  return result;
}

std::string utf8(std::wstring_view value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
      value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
  if (!size) throw std::runtime_error("Invalid Unicode string");
  std::string result(size, '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
          static_cast<int>(value.size()), result.data(), size, nullptr, nullptr) != size)
    throw std::runtime_error("Invalid Unicode string");
  return result;
}

bool noControls(std::wstring_view value) {
  return std::none_of(value.begin(), value.end(), [](wchar_t c) {
    return c < 0x20 || (c >= 0x7f && c <= 0x9f);
  });
}

bool validLabel(std::wstring_view label) {
  return !label.empty() && label.size() <= 80 && noControls(label) &&
      std::any_of(label.begin(), label.end(), [](wchar_t c) { return !std::iswspace(c); });
}

std::wstring canonicalId(std::wstring id) {
  for (auto& c : id) if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
  return id;
}

bool validId(std::wstring_view id) {
  constexpr std::wstring_view prefix = L"ROOT\\MEDIA\\";
  if (id.size() <= prefix.size() || id.size() > 200 || !noControls(id)) return false;
  const auto canonical = canonicalId(std::wstring(id));
  return canonical.compare(0, prefix.size(), prefix) == 0 &&
      std::all_of(id.begin() + prefix.size(), id.end(),
          [](wchar_t c) { return c >= L'0' && c <= L'9'; });
}

// Windows argv quoting, including backslashes preceding quotes and the closing quote.
std::wstring quote(std::wstring_view arg) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (wchar_t c : arg) {
    if (c == L'\\') { ++slashes; continue; }
    result.append(slashes * (c == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (c == L'"') result += L'\\';
    result += c;
  }
  result.append(slashes * 2, L'\\');
  result += L'"';
  return result;
}

VirtualDeviceSnapshot unavailable(std::string error) {
  VirtualDeviceSnapshot result;
  result.error = std::move(error);
  return result;
}

std::wstring helperPath() {
  std::wstring path(32768, L'\0');
  const DWORD size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  if (!size || size >= path.size()) return {};
  path.resize(size);
  const auto slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return {};
  path.resize(slash + 1);
  path += L"driver-lab\\sar_endpoint_manager.exe";
  const auto attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
  return path;
}

constexpr const char* kMissingHelper =
    "Experimental helper not installed: expected driver-lab/sar_endpoint_manager.exe "
    "beside the application. No device was created.";

struct AttributeList {
  std::vector<unsigned char> storage;
  LPPROC_THREAD_ATTRIBUTE_LIST value = nullptr;
  ~AttributeList() { if (value) DeleteProcThreadAttributeList(value); }
  bool initialize(HANDLE* handles, std::size_t count) {
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    storage.resize(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) return false;
    value = list;
    return UpdateProcThreadAttribute(value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        handles, count * sizeof(HANDLE), nullptr, nullptr) != FALSE;
  }
};

// Only this non-elevated inventory child may be terminated. Never used for PnP mutations.
struct InventoryChild {
  Handle process;
  Handle thread;
  ~InventoryChild() {
    if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT)
      TerminateProcess(process.value, ERROR_CANCELLED);
  }
};

VirtualDeviceSnapshot inspectHelper(const std::wstring& path) {
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle read, write;
  if (!CreatePipe(&read.value, &write.value, &security, 0) ||
      !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0))
    return unavailable("Could not create the experimental helper output pipe");
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
      &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (input.value == INVALID_HANDLE_VALUE) return unavailable("Could not create helper input");
  HANDLE inherited[]{write.value, input.value};
  AttributeList attributes;
  if (!attributes.initialize(inherited, 2)) return unavailable("Could not restrict helper handles");
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdOutput = write.value;
  startup.StartupInfo.hStdError = write.value;
  startup.StartupInfo.hStdInput = input.value;
  startup.lpAttributeList = attributes.value;
  PROCESS_INFORMATION info{};
  auto command = quote(path) + L" list --json";
  const auto directory = path.substr(0, path.find_last_of(L"\\/"));
  const ULONGLONG deadline = GetTickCount64() + kInspectTimeout;
  if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
          CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
          directory.c_str(), &startup.StartupInfo, &info))
    return unavailable("Could not start the experimental helper (Windows error " +
        std::to_string(GetLastError()) + ")");
  InventoryChild child;
  child.process.value = info.hProcess;
  child.thread.value = info.hThread;
  write.reset();
  input.reset();
  std::string output;
  for (;;) {
    if (GetTickCount64() >= deadline) return unavailable("Experimental helper list timed out (15 seconds)");
    DWORD bytes = 0;
    if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &bytes, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) break;
      return unavailable("Could not read experimental helper output");
    }
    if (bytes) {
      char buffer[4096];
      DWORD count = 0;
      if (!ReadFile(read.value, buffer, (std::min)(bytes, DWORD{sizeof(buffer)}), &count, nullptr))
        return unavailable("Could not read experimental helper output");
      if (output.size() + count > kMaximumOutput)
        return unavailable("Experimental helper output exceeds 1 MiB");
      output.append(buffer, count);
      continue;
    }
    const DWORD state = WaitForSingleObject(child.process.value, 10);
    if (state == WAIT_FAILED) return unavailable("Could not wait for experimental helper");
    if (state == WAIT_OBJECT_0) {
      // Check once more after exit, so the final pipe write is not dropped.
      if (!PeekNamedPipe(read.value, nullptr, 0, nullptr, &bytes, nullptr) || !bytes) break;
    }
  }
  const auto now = GetTickCount64();
  if (now >= deadline || WaitForSingleObject(child.process.value,
          static_cast<DWORD>(deadline - now)) != WAIT_OBJECT_0)
    return unavailable("Experimental helper list timed out (15 seconds)");
  DWORD exitCode = 0;
  if (!GetExitCodeProcess(child.process.value, &exitCode) || exitCode != 0)
    return unavailable("Experimental helper list failed; no reliable inventory is available");
  return parseVirtualDeviceSnapshot(output);
}

winrt::Windows::Data::Json::IJsonValue field(
    const winrt::Windows::Data::Json::JsonObject& object, const wchar_t* name,
    winrt::Windows::Data::Json::JsonValueType type) {
  const auto value = object.GetNamedValue(name);
  if (value.ValueType() != type) throw std::runtime_error("Wrong JSON field type");
  return value;
}

unsigned long integer(const winrt::Windows::Data::Json::JsonObject& object,
                      const wchar_t* name) {
  const double value = field(object, name,
      winrt::Windows::Data::Json::JsonValueType::Number).GetNumber();
  if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
      value > (std::numeric_limits<unsigned long>::max)())
    throw std::runtime_error("Invalid JSON integer");
  return static_cast<unsigned long>(value);
}

bool absoluteInf(const std::wstring& path) {
  const bool drive = path.size() >= 3 &&
      ((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) &&
      path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
  // Accept normal UNC paths, not Win32 device namespaces.
  const bool unc = path.size() > 4 && path[0] == L'\\' && path[1] == L'\\' &&
      path[2] != L'?' && path[2] != L'.' && path.find(L'\\', 2) != std::wstring::npos;
  if ((!drive && !unc) || path.size() > 32700 || !noControls(path) ||
      path.size() < 4 || canonicalId(path.substr(path.size() - 4)) != L".INF") return false;
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
}  // namespace

VirtualDeviceSnapshot parseVirtualDeviceSnapshot(const std::string& json) {
  if (json.empty() || json.size() > kMaximumOutput)
    return unavailable("Invalid experimental helper JSON size (maximum 1 MiB)");
  try {
    const bool bom = json.size() >= 3 && json.compare(0, 3, "\xEF\xBB\xBF") == 0;
    const auto text = wide(bom ? json.substr(3) : json);
    // Keep COM lifetime scoped to this blocking worker call, not the client/UI lifetime.
    Apartment apartment;
    using namespace winrt::Windows::Data::Json;
    const auto root = JsonObject::Parse(winrt::hstring(text));
    if (integer(root, L"maximum") != 4) throw std::runtime_error("Unexpected instance maximum");
    const auto items = field(root, L"instances", JsonValueType::Array).GetArray();
    if (items.Size() > 4) throw std::runtime_error("Too many instances");
    VirtualDeviceSnapshot result;
    result.maximum = 4;
    std::vector<std::wstring> ids;
    for (const auto& item : items) {
      if (item.ValueType() != JsonValueType::Object) throw std::runtime_error("Invalid instance");
      const auto object = item.GetObject();
      const auto idString = field(object, L"id", JsonValueType::String).GetString();
      const auto labelString = field(object, L"label", JsonValueType::String).GetString();
      const std::wstring id(idString.c_str(), idString.size());
      const std::wstring label(labelString.c_str(), labelString.size());
      if (!validId(id) || !validLabel(label)) throw std::runtime_error("Invalid instance ID or label");
      const auto key = canonicalId(id);
      if (std::find(ids.begin(), ids.end(), key) != ids.end())
        throw std::runtime_error("Duplicate instance ID");
      ids.push_back(key);
      VirtualDeviceInstance instance;
      instance.id = utf8(id);
      instance.label = utf8(label);
      // started is only PnP state, never evidence of WASAPI/audio readiness.
      instance.started = field(object, L"started", JsonValueType::Boolean).GetBoolean();
      instance.problem = field(object, L"problem", JsonValueType::Boolean).GetBoolean();
      instance.problemCode = integer(object, L"problemCode");
      result.instances.push_back(std::move(instance));
    }
    result.available = true;
    return result;
  } catch (...) {
    return unavailable("Invalid experimental helper JSON: expected a strict four-instance PnP inventory");
  }
}

VirtualDeviceClient::VirtualDeviceClient() = default;

VirtualDeviceSnapshot VirtualDeviceClient::inspect() const {
  try {
    const auto path = helperPath();
    return path.empty() ? unavailable(kMissingHelper) : inspectHelper(path);
  } catch (...) {
    return unavailable("Could not inspect experimental virtual devices");
  }
}

VirtualDeviceResult VirtualDeviceClient::execute(VirtualDeviceAction action,
    const std::string& instanceId, const std::string& label, const std::string& infPath) const {
  VirtualDeviceResult result;
  static MutationGate gate;
  std::unique_lock lock(gate.mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    result.message = "Another experimental virtual device mutation is in progress";
    return result;
  }
  if (gate.unknownWithoutHandle || (gate.pending.value &&
          WaitForSingleObject(gate.pending.value, 0) != WAIT_OBJECT_0)) {
    result.outcomeUnknown = true;
    result.message = "A previous privileged helper may still be running; mutations remain blocked. Inventory refresh is still available.";
    return result;
  }
  gate.pending.reset();
  bool launched = false;
  try {
    std::wstring parameters;
    switch (action) {
      case VirtualDeviceAction::Add: {
        if (label.size() > 320 || infPath.size() > 131072)
          throw std::runtime_error("Invalid Add label or INF path length");
        const auto name = wide(label);
        const auto inf = wide(infPath);
        if (!validLabel(name) || !absoluteInf(inf))
          throw std::runtime_error("Add requires a nonempty label of at most 80 characters and an existing absolute .inf file");
        parameters = L"add " + quote(inf) + L" " + quote(name);
        break;
      }
      case VirtualDeviceAction::Rename:
      case VirtualDeviceAction::Remove: {
        if (instanceId.size() > 200 || label.size() > 320)
          throw std::runtime_error("Invalid instance ID or label length");
        const auto id = wide(instanceId);
        if (!validId(id)) throw std::runtime_error("Select a valid ROOT\\MEDIA\\digits instance ID");
        if (action == VirtualDeviceAction::Rename) {
          const auto name = wide(label);
          if (!validLabel(name)) throw std::runtime_error("Rename requires a nonempty label of at most 80 characters without controls");
          parameters = L"rename " + quote(id) + L" " + quote(name);
        } else {
          parameters = L"remove " + quote(id) + L" --confirm --wait-ms 30000";
        }
        break;
      }
      default: throw std::runtime_error("Unknown virtual device action");
    }
    const auto path = helperPath();
    if (path.empty()) {
      result.message = kMissingHelper;
      result.snapshot = unavailable(kMissingHelper);
      return result;
    }
    Apartment apartment;
    const auto directory = path.substr(0, path.find_last_of(L"\\/"));
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = path.c_str();
    info.lpParameters = parameters.c_str();
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    const BOOL opened = ShellExecuteExW(&info);
    const DWORD error = opened ? ERROR_SUCCESS : GetLastError();
    Handle process(info.hProcess);
    if (!opened) {
      result.cancelled = error == ERROR_CANCELLED;
      result.message = result.cancelled ? "Administrator approval was cancelled; no mutation was launched" :
          "Could not launch the experimental helper (Windows error " + std::to_string(error) + ")";
    } else {
      launched = true;
      const DWORD waited = process.value ? WaitForSingleObject(process.value, kMutationTimeout) : WAIT_FAILED;
      DWORD code = 0;
      if (waited != WAIT_OBJECT_0 || !GetExitCodeProcess(process.value, &code)) {
        result.outcomeUnknown = true;
        gate.pending.value = std::exchange(process.value, nullptr);
        gate.unknownWithoutHandle = !gate.pending.value;
        result.message = "Mutation outcome unknown; the privileged helper was not terminated and may still be running. Refresh before retrying.";
      } else {
        result.success = code == 0;
        result.message = result.success ?
            "Experimental helper completed; refreshed inventory reports PnP state, not audio readiness" :
            "Experimental helper returned exit code " + std::to_string(code) +
                "; the device may still be installed or changed. Check refreshed inventory.";
      }
    }
    result.snapshot = inspect();
    if (!result.snapshot.available) result.message += " Inventory refresh failed: " + result.snapshot.error;
  } catch (const std::exception& error) {
    result.message = error.what();
    result.outcomeUnknown = launched;
    if (launched) result.snapshot = inspect();
  } catch (...) {
    result.message = "Experimental virtual device operation failed";
    result.outcomeUnknown = launched;
    if (launched) result.snapshot = inspect();
  }
  return result;
}

}  // namespace sar::gui_vstgui
