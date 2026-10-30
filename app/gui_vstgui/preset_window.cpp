#include "app/gui_vstgui/preset_window.h"
#include "app/gui_vstgui/localization.h"
#include "app/gui_vstgui/preset_client.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/panel_button.h"
#include "app/gui_vstgui/view_ownership.h"
#include "app/gui_vstgui/window_geometry.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace sar::gui_vstgui {
namespace {
using namespace VSTGUI;
using namespace VSTGUI::Standalone;
constexpr const char* kTitle = "Presets";
constexpr CCoord kWidth = 660, kHeight = 350;

class PresetPanel final : public CViewContainer, public IControlListener {
 public:
  PresetPanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    label(CRect(24, 16, 510, 46), tr("PRESETS"), palette::kSteel);
    refresh_ = button(CRect(534, 16, 636, 48), 1, tr("Refresh"));
    label(CRect(24, 64, 636, 88), tr("Saved presets"), palette::kMuted);
    presets_ = makeOwned<COptionMenu>(CRect(24, 92, 636, 124), this, 2);
    presets_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    presets_->setFontColor(palette::kText);
    presets_->setBackColor(palette::kRaised);
    presets_->setFrameColor(palette::kLine);
    addRetainedView(*this, presets_);
    load_ = button(CRect(24, 138, 140, 172), 3, tr("Load"));
    remove_ = button(CRect(152, 138, 268, 172), 4, tr("Delete"));
    label(CRect(24, 192, 636, 216), tr("Preset name"), palette::kMuted);
    name_ = makeOwned<CTextEdit>(CRect(24, 220, 508, 254), this, 5, "");
    name_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    name_->setFontColor(palette::kText);
    name_->setBackColor(palette::kSurface);
    name_->setFrameColor(palette::kLine);
    name_->setHoriAlign(kLeftText);
    name_->setImmediateTextChange(true);
    addRetainedView(*this, name_);
    save_ = button(CRect(520, 220, 636, 254), 6, tr("Save current"));
    message_ = makeOwned<CMultiLineTextLabel>(CRect(24, 274, 636, 330));
    message_->setText(tr("Reading presets..."));
    message_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    message_->setFontColor(palette::kMuted);
    message_->setBackColor(palette::kCanvas);
    message_->setFrameColor(palette::kCanvas);
    message_->setHoriAlign(kLeftText);
    message_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    addRetainedView(*this, message_);
    timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { tick(); }, 50, true);
    request(PresetAction::List);
  }
  ~PresetPanel() noexcept override { if (timer_) timer_->stop(); }

  void valueChanged(CControl* source) override {
    if (rendering_ || pending_.valid()) return;
    if (source == name_.get()) { updateEnabled(); return; }
    if (source == presets_.get()) {
      selected_ = selectedName();
      name_->setText(selected_.c_str());
      updateEnabled();
      return;
    }
    if (source->getValue() <= 0.5F) return;
    if (source == refresh_.get()) request(PresetAction::List);
    else if (source == save_.get()) request(PresetAction::Save, name_->getText().getString());
    else if (source == load_.get()) {
      const auto name = selectedName();
      if (!name.empty() && confirm(L"Replace the current engine routing with the selected preset?"))
        request(PresetAction::Load, name);
    } else if (source == remove_.get()) {
      const auto name = selectedName();
      if (!name.empty() && confirm(L"Permanently delete the selected preset file?"))
        request(PresetAction::Delete, name);
    }
  }

 private:
  void label(const CRect& bounds, const char* value, CColor color) {
    auto view = makeOwned<CTextLabel>(bounds, value);
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    view->setFontColor(color);
    view->setBackColor(palette::kCanvas);
    view->setFrameColor(palette::kCanvas);
    view->setHoriAlign(kLeftText);
    addRetainedView(*this, view);
  }
  SharedPointer<CTextButton> button(const CRect& bounds, int tag, const char* title) {
    auto view = makeOwned<CTextButton>(bounds, this, tag, title);
    stylePanelButton(*view);
    view->setTextColor(palette::kText);
    addRetainedView(*this, view);
    return view;
  }
  bool confirm(const wchar_t* text) const {
    const auto frame = getFrame();
    if (!frame || !frame->getPlatformFrame()) return false;
    const auto native = static_cast<HWND>(frame->getPlatformFrame()->getPlatformRepresentation());
    const auto owner = native ? GetAncestor(native, GA_ROOT) : nullptr;
    return owner && MessageBoxW(owner, trWide(text).c_str(), trWide(L"Presets").c_str(),
        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
  }
  std::string selectedName() const {
    const auto index = presets_->getCurrentIndex();
    return index >= 0 && static_cast<std::size_t>(index) < names_.size()
        ? names_[static_cast<std::size_t>(index)] : std::string{};
  }
  void message(const std::string& text, bool failed) {
    const auto localized = trText(text);
    message_->setText(localized.c_str());
    message_->setTooltipText(localized.c_str());
    message_->setFontColor(failed ? palette::kDanger : palette::kText);
  }
  void updateEnabled() {
    const bool idle = !pending_.valid();
    setPanelControlEnabled(*refresh_, idle);
    setPanelControlEnabled(*presets_, idle && !names_.empty());
    setPanelControlEnabled(*name_, idle);
    setPanelControlEnabled(*save_, idle && validPresetName(name_->getText().getString()));
    setPanelControlEnabled(*load_, idle && !selectedName().empty());
    setPanelControlEnabled(*remove_, idle && !selectedName().empty());
  }
  void request(PresetAction action, std::string name = {}, bool overwrite = false) {
    if (pending_.valid()) return;
    action_ = action;
    requestedName_ = name;
    // The task captures values only; closing this window never blocks on pipe or disk I/O.
    std::packaged_task<PresetResult()> task([action, name = std::move(name), overwrite] {
      return runPresetAction(action, name, overwrite);
    });
    auto future = task.get_future();
    try {
      std::thread(std::move(task)).detach();
      pending_ = std::move(future);
      message("Working...", false);
    } catch (...) { message("Could not start preset worker.", true); }
    updateEnabled();
  }
  void tick() {
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    PresetResult result;
    try { result = pending_.get(); }
    catch (...) { result.message = "Preset worker failed. No automatic retry was made."; }
    if (result.hasList) {
      if (result.succeeded && action_ == PresetAction::Save) selected_ = requestedName_;
      names_ = std::move(result.names);
      rendering_ = true;
      presets_->removeAllEntry();
      for (const auto& name : names_) presets_->addEntry(name.c_str());
      const auto found = std::find(names_.begin(), names_.end(), selected_);
      const auto index = found == names_.end() ? 0 : static_cast<int>(found - names_.begin());
      if (names_.empty()) presets_->addEntry(tr("No saved presets"));
      presets_->setValue(static_cast<float>(index));
      selected_ = selectedName();
      rendering_ = false;
    }
    message(result.message, !result.succeeded && !result.needsOverwrite);
    updateEnabled();
    if (result.needsOverwrite && confirm(L"A preset with this name already exists. Replace its file?"))
      request(PresetAction::Save, requestedName_, true);
  }

  bool rendering_ = false;
  PresetAction action_ = PresetAction::List;
  std::string selected_, requestedName_;
  std::vector<std::string> names_;
  std::future<PresetResult> pending_;
  SharedPointer<CVSTGUITimer> timer_;
  SharedPointer<COptionMenu> presets_;
  SharedPointer<CTextEdit> name_;
  SharedPointer<CTextButton> refresh_, load_, remove_, save_;
  SharedPointer<CMultiLineTextLabel> message_;
};

struct LastPresetWindow final : WindowListenerAdapter {
  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) IApplication::instance().quit();
  }
};
LastPresetWindow lastWindow;
}  // namespace

void showPresetWindow() {
  for (const auto& window : IApplication::instance().getWindows()) {
    if (window->getTitle().getString() == kTitle) { window->show(); window->activate(); return; }
  }
  WindowConfiguration config;
  config.type = WindowType::Document;
  config.style.border().close().size();
  config.size = CPoint(kWidth, kHeight);
  config.title = kTitle;
  auto window = IApplication::instance().createWindow(config, std::make_shared<WindowControllerAdapter>());
  if (!window) return;
  auto frame = makeOwned<CFrame>(CRect(0, 0, kWidth, kHeight), nullptr);
  frame->setFocusDrawingEnabled(true);
  frame->setFocusColor(palette::kSteel);
  auto viewport = makeOwned<ThemedScrollView>(CRect(0, 0, kWidth, kHeight),
      CRect(0, 0, kWidth, kHeight), CScrollView::kHorizontalScrollbar |
      CScrollView::kVerticalScrollbar | CScrollView::kAutoHideScrollbars |
      CScrollView::kDontDrawFrame | CScrollView::kFollowFocusView);
  viewport->setBackgroundColor(palette::kCanvas);
  styleScrollbars(*viewport);
  viewport->setAutosizeFlags(kAutosizeLeft | kAutosizeTop | kAutosizeRight | kAutosizeBottom);
  addRetainedView(*viewport, makeOwned<PresetPanel>());
  addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWidth, kHeight),
      MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST));
  window->registerWindowListener(&lastWindow);
  window->show();
}
}  // namespace sar::gui_vstgui
