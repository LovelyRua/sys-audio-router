#pragma once

#include "vstgui/lib/cviewcontainer.h"

namespace sar::gui_vstgui {

// VSTGUI 4.9 consumes an extra initial reference in removeView/removeAll,
// in addition to its internal SharedPointer. Give that reference explicitly
// when the caller already owns the view through makeOwned/SharedPointer.
template <class View>
bool addRetainedView(VSTGUI::CViewContainer& parent,
                     const VSTGUI::SharedPointer<View>& view) {
  if (!view) return false;
  view->remember();
  if (parent.addView(view.get())) return true;
  view->forget();
  return false;
}

}  // namespace sar::gui_vstgui
