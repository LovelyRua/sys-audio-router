#pragma once

#include "app/gui_vstgui/palette.h"
#include "vstgui/lib/controls/cbuttons.h"

namespace sar::gui_vstgui {

inline void stylePanelButton(VSTGUI::CTextButton& button) {
  // CTextButton skips its fill AND outline when either gradient is null.
  // Equal endpoints give flat fills while retaining native control behaviour.
  auto normal = VSTGUI::owned(VSTGUI::CGradient::create(
      0, 1, palette::kRaised, palette::kRaised));
  auto pressed = VSTGUI::owned(VSTGUI::CGradient::create(
      0, 1, palette::kLine, palette::kLine));
  button.setGradient(normal);
  button.setGradientHighlighted(pressed);
  button.setFrameColor(palette::kLine);
  button.setFrameColorHighlighted(palette::kSteel);
  button.setTextColorHighlighted(palette::kText);
  button.setRoundRadius(2);
}

inline void setPanelControlEnabled(VSTGUI::CView& view, bool enabled) {
  view.setMouseEnabled(enabled);
  view.setAlphaValue(enabled ? 1.0F : 0.4F);
}

}  // namespace sar::gui_vstgui
