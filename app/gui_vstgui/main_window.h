#pragma once

#include "vstgui/standalone/include/iwindow.h"

namespace sar::gui_vstgui {

// Builds and shows the main window: the header bar (brand, connection
// status, LCD readouts, start/stop) over a placeholder body. This is the
// first slice of the VSTGUI control panel -- see docs/product-readiness.md
// for what has not been ported yet (routing matrix, devices, diagnostics
// detail, presets, dialogs, localization).
[[nodiscard]] VSTGUI::Standalone::WindowPtr createMainWindow();

}  // namespace sar::gui_vstgui
