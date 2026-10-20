#pragma once

#include "vstgui/standalone/include/iwindow.h"

namespace sar::gui_vstgui {

// Builds the fixed-size control window with engine status and a paged route
// matrix. Experimental virtual WASAPI adapter management opens a separate
// non-realtime window; general runtime configuration remains in the Qt GUI.
[[nodiscard]] VSTGUI::Standalone::WindowPtr createMainWindow();

}  // namespace sar::gui_vstgui
