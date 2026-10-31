#pragma once

#include "vstgui/standalone/include/iwindow.h"

namespace sar::gui_vstgui {

// Builds the resizable control window with shared navigation, engine status,
// and a paged route matrix. Editors run outside the realtime processing path.
[[nodiscard]] VSTGUI::Standalone::WindowPtr createMainWindow();

}  // namespace sar::gui_vstgui
