#pragma once

#include <string>

namespace sar::gui_vstgui {

enum class UiLanguage { System = 0, English = 1, Chinese = 2 };

// Stored preference only; the QA environment override is never persisted.
UiLanguage languagePreference();
bool setLanguagePreference(UiLanguage language);
// Call at GUI startup. Repeated calls never change the selected language.
void initializeLanguage();
bool chineseLanguage();
// Translate application-owned text only, never device/user labels or codes.
const char* tr(const char* english);
std::string trText(const std::string& english);
std::wstring trWide(const wchar_t* english);

}  // namespace sar::gui_vstgui
