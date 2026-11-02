#include "app/gui_vstgui/preset_client.h"

#include <cassert>
#include <string>

int main() {
  using namespace sar::gui_vstgui;
  for (const auto* name : {"Main", "Broadcast mix", "Mix.2", "COM10", "Console"})
    assert(validPresetName(name));
  assert(validPresetName(std::string(80, 'a')));
  assert(!validPresetName(std::string(81, 'a')));
  assert(validPresetName("\xe6\xb7\xb7\xe9\x9f\xb3"));
  for (const auto* name : {"", ".", "..", " leading", "trailing ", "trailing.",
       "CON", "con.txt", "NUL", "PRN", "AUX", "COM1", "lpt9.ext", "CONIN$",
       "CONOUT$", "../escape", "folder/preset", "folder\\preset", "C:drive",
       "a?b", "a*b", "a<b", "a>b", "a|b", "a\"b", "line\nbreak", "tab\tname"}) {
    assert(!validPresetName(name));
    // All mutating operations reject invalid input before touching disk or IPC.
    for (auto action : {PresetAction::Save, PresetAction::Load, PresetAction::Delete}) {
      const auto result = runPresetAction(action, name);
      assert(!result.succeeded && !result.hasList && !result.needsOverwrite);
      assert(!result.message.empty());
    }
  }
  assert(!validPresetName(std::string("nul\0hidden", 10)));
  assert(!validPresetName("\xc0\xaf")); // Invalid UTF-8 must never reach a filesystem path.
  assert(!validPresetName("name\xc2\xa0"));
  assert(!validPresetName("\xe3\x80\x80name"));
  assert(!validPresetName("COM\xc2\xb9"));
}
