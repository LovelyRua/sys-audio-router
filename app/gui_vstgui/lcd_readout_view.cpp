#include "app/gui_vstgui/lcd_readout_view.h"

#include "app/gui_vstgui/palette.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

using namespace VSTGUI;

namespace sar::gui_vstgui {

LcdReadoutView::LcdReadoutView(const CRect& size)
    : CView(size), tint_(palette::kLcdAmber) {}

void LcdReadoutView::setText(const std::string& text) {
  if (text_ == text) {
    return;
  }
  text_ = text;
  invalid();
}

void LcdReadoutView::setTint(CColor tint) {
  if (tint_ == tint) {
    return;
  }
  tint_ = tint;
  invalid();
}

void LcdReadoutView::draw(CDrawContext* context) {
  const auto rect = getViewSize();

  context->setFillColor(palette::kLcdBackground);
  context->setFrameColor(CColor(0, 0, 0));
  context->setLineWidth(1);
  context->drawRect(rect, kDrawFilledAndStroked);

  static const auto font = makeOwned<CFontDesc>("Consolas", 12);
  context->setFont(font);
  context->setFontColor(tint_);
  context->drawString(text_.c_str(), rect, kCenterText, true);
}

}  // namespace sar::gui_vstgui
