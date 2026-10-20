#include "app/gui_vstgui/virtual_device_window.h"
#include "app/gui_vstgui/view_ownership.h"
#include "app/gui_vstgui/virtual_device_client.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/window_geometry.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cscrollview.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/platform/iplatformframe.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/iwindow.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <iterator>
#include <memory>
#include <thread>

namespace sar::gui_vstgui {
namespace {
using namespace VSTGUI;
using namespace VSTGUI::Standalone;
constexpr auto kTitle = "Virtual WASAPI (lab)";
constexpr CCoord kWidth = 860;
constexpr CCoord kHeight = 560;

SharedPointer<CTextLabel> label(const CRect& bounds, const char* text,
                               CColor color = palette::kText) {
  auto view = makeOwned<CTextLabel>(bounds, text);
  view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
  view->setBackColor(palette::kCanvas);
  view->setFrameColor(palette::kCanvas);
  view->setFontColor(color);
  view->setHoriAlign(kLeftText);
  return view;
}

class DevicePanel final : public CViewContainer, public IControlListener {
 public:
  DevicePanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    addRetainedView(*this, label(CRect(24, 16, 600, 46), "VIRTUAL WASAPI", palette::kSteel));
    refresh_ = button(CRect(732, 16, 836, 46), "Refresh", 1);
    capacity_ = label(CRect(24, 54, 836, 80), "Inspecting installed adapters...");
    addRetainedView(*this, capacity_);
    addRetainedView(*this, label(CRect(24, 94, 160, 124), "Adapter", palette::kMuted));
    adapters_ = makeOwned<COptionMenu>(CRect(160, 94, 836, 126), this, 2);
    adapters_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    adapters_->setBackColor(palette::kRaised);
    adapters_->setFontColor(palette::kText);
    adapters_->setFrameColor(palette::kLine);
    addRetainedView(*this, adapters_);
    identity_ = label(CRect(160, 134, 836, 158), "");
    state_ = label(CRect(160, 164, 836, 190), "");
    addRetainedView(*this, identity_);
    addRetainedView(*this, state_);
    addRetainedView(*this, label(CRect(24, 202, 836, 232),
        "2 stereo bus pairs per adapter  /  48 kHz  /  fixed channel topology", palette::kMuted));
    addRetainedView(*this, label(CRect(24, 246, 160, 278), "Adapter label", palette::kMuted));
    name_ = edit(CRect(160, 246, 704, 278), "SAR virtual adapter", 3);
    rename_ = button(CRect(716, 246, 836, 278), "Rename", 4);
    addRetainedView(*this, label(CRect(24, 296, 160, 328), "Driver INF", palette::kMuted));
    inf_ = edit(CRect(160, 296, 782, 328), "", 5);
    inf_->setPlaceholderString("Signed experimental driver package (.inf)");
    browse_ = button(CRect(794, 296, 836, 328), "...", 6);
    browse_->setTooltipText("Choose the experimental driver INF");
    add_ = button(CRect(160, 348, 324, 384), "+ Add adapter", 7);
    remove_ = button(CRect(672, 348, 836, 384), "Remove adapter", 8);
    message_ = makeOwned<CMultiLineTextLabel>(CRect(24, 408, 836, 532));
    message_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    message_->setFontColor(palette::kMuted);
    message_->setBackColor(palette::kCanvas);
    message_->setFrameColor(palette::kCanvas);
    message_->setHoriAlign(kLeftText);
    message_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    addRetainedView(*this, message_);
    timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { complete(); }, 50, true);
    inspect();
  }

  ~DevicePanel() noexcept override { timer_->stop(); }

  void valueChanged(CControl* control) override {
    SharedPointer<DevicePanel> keepAlive(this);
    if (control == adapters_.get()) {
      selectedId_.clear();
      const auto index = adapters_->getCurrentIndex();
      if (index >= 0 && static_cast<std::size_t>(index) < snapshot_.instances.size()) {
        selectedId_ = snapshot_.instances[static_cast<std::size_t>(index)].id;
      }
      renderSelection(true);
      return;
    }
    if (control == name_.get() || control == inf_.get()) {
      updateEnabled();
      return;
    }
    if (pending_.valid() || control->getValue() <= 0.5F) return;
    if (control == refresh_.get()) { inspect(); return; }
    if (control == browse_.get()) { browse(); return; }
    const auto action = control == add_.get() ? VirtualDeviceAction::Add
                      : control == rename_.get() ? VirtualDeviceAction::Rename
                                                : VirtualDeviceAction::Remove;
    if (action != VirtualDeviceAction::Add && selectedId_.empty()) return;
    if (action == VirtualDeviceAction::Remove) {
      const auto owner = dialogOwner();
      if (!owner) return;
      const int answer = MessageBoxW(owner,
          L"Remove the selected virtual adapter and both of its stereo bus pairs?\n\n"
          L"Applications using these endpoints will lose their audio connection.\n"
          L"The shared driver package will remain installed.",
          L"Remove SAR virtual adapter", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING);
      if (answer != IDYES || !getFrame()) return;
    }
    const auto id = selectedId_;
    const auto name = name_->getText().getString();
    const auto inf = inf_->getText().getString();
    message_->setText("Waiting for Windows device management...");
    launch([action, id, name, inf] {
      return VirtualDeviceClient{}.execute(action, id, name, inf);
    });
  }

 private:
  SharedPointer<CTextButton> button(const CRect& bounds, const char* text, int tag) {
    auto view = makeOwned<CTextButton>(bounds, this, tag, text);
    view->setGradient(nullptr);
    view->setFrameColor(palette::kLine);
    view->setTextColor(palette::kText);
    view->setRoundRadius(2);
    addRetainedView(*this, view);
    return view;
  }

  SharedPointer<CTextEdit> edit(const CRect& bounds, const char* text, int tag) {
    auto view = makeOwned<CTextEdit>(bounds, this, tag, text);
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    view->setFontColor(palette::kText);
    view->setBackColor(palette::kRaised);
    view->setFrameColor(palette::kLine);
    view->setHoriAlign(kLeftText);
    view->setImmediateTextChange(true);
    addRetainedView(*this, view);
    return view;
  }

  template <class Work> void launch(Work work) {
    // A promise-backed future does not block window destruction. The detached
    // worker owns only copied arguments, never this panel or its views.
    std::packaged_task<VirtualDeviceResult()> task(std::move(work));
    auto future = task.get_future();
    try {
      std::thread(std::move(task)).detach();
      pending_ = std::move(future);
    } catch (...) {
      message_->setText("Could not start the device management worker.");
    }
    updateEnabled();
  }

  void inspect() {
    message_->setText("Reading installed virtual adapters...");
    launch([] {
      VirtualDeviceResult result;
      result.snapshot = VirtualDeviceClient{}.inspect();
      result.success = result.snapshot.available;
      result.message = result.snapshot.error;
      return result;
    });
  }

  void complete() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
      auto result = pending_.get();
      const auto previousId = selectedId_;
      if (result.snapshot.available || !result.snapshot.error.empty()) {
        snapshot_ = std::move(result.snapshot);
      }
      message_->setFontColor(result.success || result.cancelled ? palette::kMuted : palette::kDanger);
      message_->setText(result.message.empty()
          ? snapshot_.available ? "" : "Virtual device inventory is unavailable."
          : result.message.c_str());
      adapters_->removeAllEntry();
      int selected = -1;
      for (std::size_t i = 0; i < snapshot_.instances.size(); ++i) {
        const auto& instance = snapshot_.instances[i];
        const auto suffix = instance.id.substr(instance.id.find_last_of('\\') + 1);
        adapters_->addEntry((instance.label + " [" + suffix + "]").c_str());
        if (instance.id == selectedId_) selected = static_cast<int>(i);
      }
      if (selected < 0 && !snapshot_.instances.empty()) selected = 0;
      if (selected >= 0) {
        adapters_->setCurrent(selected);
        selectedId_ = snapshot_.instances[static_cast<std::size_t>(selected)].id;
      } else {
        selectedId_.clear();
        adapters_->addEntry("No installed adapters");
        adapters_->setCurrent(0);
      }
      const auto capacity = std::to_string(snapshot_.instances.size()) + " / " +
          std::to_string(snapshot_.maximum) + " experimental adapters";
      capacity_->setText(snapshot_.available ? capacity.c_str() : "Experimental helper unavailable");
      renderSelection(previousId != selectedId_);
    } catch (...) {
      snapshot_ = {};
      selectedId_.clear();
      message_->setText("Device management failed. Refresh to read the actual installed state.");
      renderSelection(false);
    }
    updateEnabled();
  }

  void renderSelection(bool copyLabel) {
    const auto found = std::find_if(snapshot_.instances.begin(), snapshot_.instances.end(),
        [this](const auto& instance) { return instance.id == selectedId_; });
    if (found == snapshot_.instances.end()) {
      identity_->setText("");
      state_->setText("");
    } else {
      identity_->setText(found->id.c_str());
      const auto status = found->problem ? "PnP problem " + std::to_string(found->problemCode)
                         : found->started ? std::string("PnP started") : std::string("PnP start pending");
      state_->setText(status.c_str());
      state_->setFontColor(found->problem ? palette::kWarning : palette::kText);
      if (copyLabel) name_->setText(found->label.c_str());
    }
    updateEnabled();
  }

  void updateEnabled() {
    const auto enable = [](CView* view, bool active) {
      view->setMouseEnabled(active);
      view->setAlphaValue(active ? 1.0F : 0.4F);
    };
    const bool idle = !pending_.valid();
    const bool selected = snapshot_.available && !selectedId_.empty();
    enable(refresh_, idle);
    enable(adapters_, idle && !snapshot_.instances.empty());
    enable(browse_, idle);
    enable(name_, idle);
    enable(inf_, idle);
    const bool named = !name_->getText().getString().empty();
    enable(add_, idle && snapshot_.available && named && !inf_->getText().getString().empty() &&
                          snapshot_.instances.size() < snapshot_.maximum);
    enable(rename_, idle && selected && named);
    enable(remove_, idle && selected);
  }

  void browse() {
    SharedPointer<DevicePanel> keepAlive(this);
    const auto owner = dialogOwner();
    if (!owner) return;
    wchar_t path[32768] = {};
    OPENFILENAMEW request{};
    request.lStructSize = sizeof(request);
    request.hwndOwner = owner;
    request.lpstrFile = path;
    request.nMaxFile = static_cast<DWORD>(std::size(path));
    request.lpstrFilter = L"Driver INF (*.inf)\0*.inf\0\0";
    request.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    const BOOL accepted = GetOpenFileNameW(&request);
    const DWORD dialogError = accepted ? 0 : CommDlgExtendedError();
    if (!getFrame()) return;
    if (!accepted) {
      if (dialogError != 0) {
        const auto diagnostic = "Could not open the driver INF picker "
            "(CommDlgExtendedError " + std::to_string(dialogError) + "). The INF path is unchanged.";
        message_->setFontColor(palette::kDanger);
        message_->setText(diagnostic.c_str());
      }
      return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return;
    std::string value(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                            value.data(), size, nullptr, nullptr) != size) return;
    value.pop_back();
    inf_->setText(value.c_str());
    updateEnabled();
  }

  HWND dialogOwner() const {
    const auto frame = getFrame();
    if (!frame || !frame->getPlatformFrame()) return nullptr;
    const auto native = static_cast<HWND>(frame->getPlatformFrame()->getPlatformRepresentation());
    return native ? GetAncestor(native, GA_ROOT) : nullptr;
  }

  VirtualDeviceSnapshot snapshot_;
  std::string selectedId_;
  std::future<VirtualDeviceResult> pending_;
  SharedPointer<CVSTGUITimer> timer_;
  SharedPointer<COptionMenu> adapters_;
  SharedPointer<CTextEdit> name_, inf_;
  SharedPointer<CTextLabel> capacity_, identity_, state_;
  SharedPointer<CMultiLineTextLabel> message_;
  SharedPointer<CTextButton> refresh_, rename_, browse_, add_, remove_;
};

struct LastWindowListener final : WindowListenerAdapter {
  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) IApplication::instance().quit();
  }
};
LastWindowListener lastWindowListener;
}  // namespace

void showVirtualDeviceWindow() {
  using namespace VSTGUI;
  using namespace VSTGUI::Standalone;
  const auto monitor = MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
  for (const auto& window : IApplication::instance().getWindows()) {
    if (window->getTitle().getString() == kTitle) {
      window->show();
      window->activate();
      return;
    }
  }
  WindowConfiguration config;
  config.type = WindowType::Document;
  config.style.border().close();
  config.size = CPoint(kWidth, kHeight);
  config.title = kTitle;
  auto window = IApplication::instance().createWindow(config,
      std::make_shared<WindowControllerAdapter>());
  if (!window) return;
  auto frame = makeOwned<CFrame>(CRect(0, 0, kWidth, kHeight), nullptr);
  frame->setFocusDrawingEnabled(true);
  frame->setFocusColor(palette::kSteel);
  auto viewport = makeOwned<CScrollView>(CRect(0, 0, kWidth, kHeight),
      CRect(0, 0, kWidth, kHeight), CScrollView::kHorizontalScrollbar |
      CScrollView::kVerticalScrollbar | CScrollView::kAutoHideScrollbars |
      CScrollView::kDontDrawFrame | CScrollView::kFollowFocusView);
  viewport->setBackgroundColor(palette::kCanvas);
  viewport->setAutosizeFlags(kAutosizeLeft | kAutosizeTop | kAutosizeRight | kAutosizeBottom);
  addRetainedView(*viewport, makeOwned<DevicePanel>());
  addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWidth, kHeight), monitor);
  window->registerWindowListener(&lastWindowListener);
  window->show();
}
}  // namespace sar::gui_vstgui
