#pragma once

#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cview.h"

#include <string>

namespace sar::gui_vstgui {

// A small inset "hardware LCD" readout: a near-black recessed panel with
// amber monospace text, used for the header's transport-style counters
// (sample rate, block size, xrun count). Hand-drawn rather than a styled
// CTextLabel so it reads as a physical display rather than a text field --
// the same intent as Main.qml's LcdReadout component.
class LcdReadoutView final : public VSTGUI::CView {
 public:
  explicit LcdReadoutView(const VSTGUI::CRect& size);

  void setText(const std::string& text);
  void setTint(VSTGUI::CColor tint);

  void draw(VSTGUI::CDrawContext* context) override;

 private:
  std::string text_;
  VSTGUI::CColor tint_;
};

}  // namespace sar::gui_vstgui
