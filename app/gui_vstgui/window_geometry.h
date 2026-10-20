#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/standalone/include/iwindow.h"

#include <algorithm>
#include <cmath>

namespace sar::gui_vstgui {

// Call after setContentView and before show, without the centered window style.
// The content must live in an autosizing scroll viewport, not be scaled to fit.
inline bool prepareScrollableWindow(VSTGUI::Standalone::IWindow& window,
                                    VSTGUI::CFrame& frame,
                                    const VSTGUI::CPoint& preferredSize,
                                    HMONITOR monitor) {
  if (!frame.getPlatformFrame()) return false;
  const auto native = static_cast<HWND>(frame.getPlatformFrame()->getPlatformRepresentation());
  const auto owner = native ? GetAncestor(native, GA_ROOT) : nullptr;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!owner || !GetMonitorInfoW(monitor, &info)) return false;
  const auto& work = info.rcWork;
  const LONG width = work.right - work.left;
  const LONG height = work.bottom - work.top;
  if (width <= 0 || height <= 0) return false;

  // Establish the target monitor before asking VSTGUI for its DPI scale.
  // The temporary outer rectangle also fits monitors smaller than the content.
  if (!SetWindowPos(owner, nullptr, work.left, work.top, width, height,
                    SWP_NOZORDER | SWP_NOACTIVATE)) return false;
  const double scale = window.getScaleFactor();
  if (!std::isfinite(scale) || scale <= 0) return false;

  const auto style = static_cast<DWORD>(GetWindowLongPtrW(owner, GWL_STYLE));
  const auto extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(owner, GWL_EXSTYLE));
  // VSTGUI 4.9 reserves menu space for bordered standalone windows even when
  // there are no commands. Match its legacy adjustment as well as DPI chrome.
  const BOOL menu = window.getStyle().hasBorder() || GetMenu(owner) != nullptr;
  RECT legacy{};
  if (!AdjustWindowRectEx(&legacy, style, menu, extendedStyle)) return false;
  RECT chrome = legacy;
  using AdjustForDpi = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
  const auto user32 = GetModuleHandleW(L"user32.dll");
  const auto adjustForDpi = user32 ? reinterpret_cast<AdjustForDpi>(
      GetProcAddress(user32, "AdjustWindowRectExForDpi")) : nullptr;
  RECT dpiChrome{};
  if (adjustForDpi && adjustForDpi(&dpiChrome, style, menu, extendedStyle,
                                  static_cast<UINT>(std::lround(scale * 96.0)))) {
    chrome.left = std::min(chrome.left, dpiChrome.left);
    chrome.top = std::min(chrome.top, dpiChrome.top);
    chrome.right = std::max(chrome.right, dpiChrome.right);
    chrome.bottom = std::max(chrome.bottom, dpiChrome.bottom);
  }
  const LONG borderWidth = chrome.right - chrome.left;
  const LONG borderHeight = chrome.bottom - chrome.top;
  if (width <= borderWidth || height <= borderHeight) return false;
  const LONG margin = std::min({static_cast<LONG>(std::ceil(8 * scale)),
      (width - borderWidth - 1) / 2, (height - borderHeight - 1) / 2});
  const VSTGUI::CPoint viewport(
      std::min(preferredSize.x, std::floor((width - borderWidth - 2 * margin) / scale)),
      std::min(preferredSize.y, std::floor((height - borderHeight - 2 * margin) / scale)));
  if (viewport.x < 1 || viewport.y < 1) return false;
  window.setSize(viewport);
  const auto outerWidth = static_cast<LONG>(viewport.x * scale) + borderWidth;
  const auto outerHeight = static_cast<LONG>(viewport.y * scale) + borderHeight;
  // IWindow positions are client origins in global coordinates on Win32.
  window.setPosition(VSTGUI::CPoint(work.left + (width - outerWidth) / 2 - legacy.left,
                                   work.top + (height - outerHeight) / 2 - legacy.top));
  return true;
}

}  // namespace sar::gui_vstgui
