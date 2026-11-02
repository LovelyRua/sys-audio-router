#include "app/gui_vstgui/vb_cable_setup.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <softpub.h>
#include <wintrust.h>
#include <filesystem>
#include <vector>

namespace sar::gui_vstgui {
VbCableSetupResult openVbCableSetup() {
  std::vector<wchar_t> buffer(32768);
  const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) return {true, "Could not locate the installed package"};
  const auto root = std::filesystem::path(std::wstring(buffer.data(), length)).parent_path().parent_path();
  const auto setup = root / L"vendor" / L"VB-CABLE" / L"VBCABLE_Setup_x64.exe";
  std::error_code error;
  if (!std::filesystem::is_regular_file(setup, error))
    return {true, "VB-CABLE setup is not included in this build. Use the official website or install your own package."};

  WINTRUST_FILE_INFO file{};
  file.cbStruct = sizeof(file);
  file.pcwszFilePath = setup.c_str();
  WINTRUST_DATA trust{};
  trust.cbStruct = sizeof(trust);
  trust.dwUIChoice = WTD_UI_NONE;
  trust.fdwRevocationChecks = WTD_REVOKE_NONE;
  trust.dwUnionChoice = WTD_CHOICE_FILE;
  trust.pFile = &file;
  trust.dwStateAction = WTD_STATEACTION_VERIFY;
  GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
  const auto status = WinVerifyTrust(nullptr, &policy, &trust);
  trust.dwStateAction = WTD_STATEACTION_CLOSE;
  WinVerifyTrust(nullptr, &policy, &trust);
  if (status != ERROR_SUCCESS) return {true, "VB-CABLE setup signature could not be verified. Setup was not opened."};

  const auto directory = setup.parent_path();
  SHELLEXECUTEINFOW execute{};
  execute.cbSize = sizeof(execute);
  execute.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  execute.lpVerb = L"runas";
  execute.lpFile = setup.c_str();
  execute.lpDirectory = directory.c_str();
  execute.nShow = SW_SHOWNORMAL;
  if (!ShellExecuteExW(&execute)) {
    if (GetLastError() == ERROR_CANCELLED) return {false, "VB-CABLE setup elevation cancelled. No automatic retry."};
    return {true, "Could not open the vendor installer. No automatic retry."};
  }
  if (execute.hProcess) CloseHandle(execute.hProcess);
  return {false, "Vendor setup opened, not yet installed. Complete its prompts, reboot if requested, then Refresh. SAR will not uninstall VB-CABLE."};
}
}
