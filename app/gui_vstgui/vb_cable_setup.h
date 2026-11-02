#pragma once
#include <string>

namespace sar::gui_vstgui {
struct VbCableSetupResult {
  bool failed = false;
  std::string message;
};
// Opens the unmodified vendor setup. Does not install silently or accept terms.
VbCableSetupResult openVbCableSetup();
}
