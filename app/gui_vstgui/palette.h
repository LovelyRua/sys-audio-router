#pragma once

#include "vstgui/lib/ccolor.h"

namespace sar::gui_vstgui::palette {

// Mirrors app/gui/qml/Main.qml's `colors` object: a neutral hardware-console
// palette (not blue-tinted) with an amber LCD accent, in the spirit of
// Steinberg Nuendo/Cubase. Keep the two in sync by eye until one GUI
// supersedes the other (see docs/product-readiness.md).
inline constexpr VSTGUI::CColor kCanvas{25, 25, 27};
inline constexpr VSTGUI::CColor kSurface{32, 32, 35};
inline constexpr VSTGUI::CColor kRaised{38, 38, 42};
inline constexpr VSTGUI::CColor kLine{58, 58, 62};
inline constexpr VSTGUI::CColor kText{217, 217, 219};
inline constexpr VSTGUI::CColor kMuted{135, 135, 140};
inline constexpr VSTGUI::CColor kHealthy{90, 157, 110};
inline constexpr VSTGUI::CColor kSteel{111, 160, 201};
inline constexpr VSTGUI::CColor kWarning{209, 171, 62};
inline constexpr VSTGUI::CColor kDanger{201, 82, 74};
inline constexpr VSTGUI::CColor kLcdBackground{10, 10, 10};
inline constexpr VSTGUI::CColor kLcdAmber{232, 165, 61};
inline constexpr VSTGUI::CColor kLcdDim{122, 90, 40};

}  // namespace sar::gui_vstgui::palette
