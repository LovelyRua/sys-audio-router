#pragma once

#include "vstgui/standalone/include/iwindow.h"

namespace sar::gui_vstgui {

// Builds the fixed-size control window with engine status and a paged route
// matrix. Device configuration and preset management remain in the Qt GUI.
[[nodiscard]] VSTGUI::Standalone::WindowPtr createMainWindow();

}  // namespace sar::gui_vstgui
