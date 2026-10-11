#pragma once

#include "vstgui/standalone/include/iwindow.h"

namespace sar::gui_vstgui {

// Builds the fixed-size control window with engine status, a paged route matrix,
// WASAPI setup, and live engine/endpoint diagnostics. Preset management remains
// in the Qt GUI.
[[nodiscard]] VSTGUI::Standalone::WindowPtr createMainWindow();

}  // namespace sar::gui_vstgui
