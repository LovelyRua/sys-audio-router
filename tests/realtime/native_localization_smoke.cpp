#include "app/gui_vstgui/localization.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace sar::gui_vstgui;
static_assert(static_cast<int>(UiLanguage::System) == 0);
static_assert(static_cast<int>(UiLanguage::English) == 1);
static_assert(static_cast<int>(UiLanguage::Chinese) == 2);

int main() {
  int failures = 0;
  const auto check = [&](bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
  };
  check(!chineseLanguage(), "Language must not initialize before startup");
  check(std::strcmp(tr("Refresh"), "Refresh") == 0, "Pre-startup fallback");
  if (!SetEnvironmentVariableW(L"SAR_NATIVE_UI_LANGUAGE", L"zh_CN")) return 1;
  initializeLanguage();
  check(chineseLanguage(), "Chinese QA override");
  check(std::strcmp(tr("Refresh"), "刷新") == 0, "UTF-8 translation");
  check(trText("Audio devices") == "音频设备", "Runtime catalog");
  check(trText("ROUTING MATRIX") == "路由矩阵", "Main catalog");
  check(trText("Virtual ASIO devices") == "虚拟 ASIO 设备", "ASIO catalog");
  check(trText("Save current") == "保存当前配置", "Preset catalog");
  check(std::string("1") + tr(" saved presets") == "1 个已保存的预设", "Preset count fragment");
  check(std::string(tr("Saved preset: ")) + "Refresh" == "已保存预设：Refresh", "Preset name retained");
  check(trText("Processed blocks") == "已处理块数", "Diagnostics catalog");
  check(trWide(L"Refresh") == L"刷新", "Wide translation");
  check(trWide(L"Permanently delete the selected preset file?") ==
      L"永久删除所选预设文件？", "Wide confirmation");
  const char unknown[] = "User Refresh device / E_FAIL 0x80004005";
  check(tr(unknown) == unknown, "Unknown pointer must be preserved");
  for (const char* text : {"refresh", " Refresh", "Refresh ", "Refresh failed", "", "E_FAIL"})
    check(std::strcmp(tr(text), text) == 0, "Exact-match fallback");
  const std::string embedded("Refresh\0user", 12);
  check(trText(embedded) == embedded, "Embedded NUL exact fallback");
  check(trText(unknown) == unknown, "Dynamic text fallback");
  check(trWide(L"用户设备 Refresh / E_FAIL") == L"用户设备 Refresh / E_FAIL", "Wide fallback");
  check(tr(nullptr) == nullptr && trWide(nullptr).empty(), "Null input");
  if (!SetEnvironmentVariableW(L"SAR_NATIVE_UI_LANGUAGE", L"en")) return 1;
  initializeLanguage();
  check(chineseLanguage() && trText("Refresh") == "刷新", "Startup choice must remain frozen");
  SetEnvironmentVariableW(L"SAR_NATIVE_UI_LANGUAGE", nullptr);
  return failures ? 1 : 0;
}
