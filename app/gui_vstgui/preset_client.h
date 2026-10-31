#pragma once

#include <string>
#include <vector>

namespace sar::gui_vstgui {

enum class PresetAction { List, Save, Load, Delete };
struct PresetResult {
  bool succeeded = false;
  bool needsOverwrite = false;
  bool hasList = false;
  std::vector<std::string> names;
  std::string message;
};

// Blocking control-plane work: invoke only on a worker thread. No GUI lifetime
// is captured. A process-wide gate bounds work across close/reopen cycles.
[[nodiscard]] PresetResult runPresetAction(PresetAction action,
                                          const std::string& name = {},
                                          bool overwriteConfirmed = false);
[[nodiscard]] bool validPresetName(const std::string& name);

}  // namespace sar::gui_vstgui
