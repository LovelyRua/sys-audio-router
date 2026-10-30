#include "app/gui_vstgui/asio_device_window.h"
#include "app/gui_vstgui/asio_device_model.h"
#include "app/gui_vstgui/engine_client.h"
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
#include <objbase.h>
#include <chrono>
#include <future>
#include <thread>

namespace sar::gui_vstgui {
namespace {
using namespace VSTGUI;
using namespace VSTGUI::Standalone;
using Clock = std::chrono::steady_clock;
constexpr CCoord kWidth = 800, kHeight = 530;
constexpr const char* kTitle = "Virtual ASIO devices";

class AsioPanel final : public CViewContainer, public IControlListener {
 public:
  AsioPanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    label(CRect(24, 16, 610, 44), "VIRTUAL ASIO DEVICES", palette::kSteel);
    refresh_ = button(CRect(666, 16, 776, 48), 1, "Refresh");
    status_ = label(CRect(24, 54, 776, 84), "Reading engine...");
    devices_ = makeOwned<COptionMenu>(CRect(24, 96, 776, 130), this, 2);
    devices_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    devices_->setFontColor(palette::kText);
    devices_->setBackColor(palette::kRaised);
    devices_->setFrameColor(palette::kLine);
    addRetainedView(*this, devices_);
    add_ = button(CRect(24, 144, 142, 178), 3, "+ Device");
    remove_ = button(CRect(154, 144, 272, 178), 4, "Remove");
    reload_ = button(CRect(624, 144, 776, 178), 5, "Reload saved");
    label(CRect(24, 194, 168, 222), "Device name", palette::kMuted);
    name_ = edit(CRect(180, 194, 776, 226), 6);
    label(CRect(24, 242, 168, 270), "DAW inputs", palette::kMuted);
    inputs_ = edit(CRect(180, 242, 292, 274), 7);
    label(CRect(330, 242, 458, 270), "DAW outputs", palette::kMuted);
    outputs_ = edit(CRect(474, 242, 586, 274), 8);
    enabled_ = makeOwned<CCheckBox>(CRect(624, 242, 776, 274), this, 9, "Enabled");
    enabled_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    enabled_->setFontColor(palette::kText);
    enabled_->setBoxFillColor(palette::kCanvas);
    enabled_->setBoxFrameColor(palette::kLine);
    enabled_->setCheckMarkColor(palette::kHealthy);
    enabled_->setRoundRectRadius(2);
    addRetainedView(*this, enabled_);
    identity_ = label(CRect(24, 294, 776, 322), "", palette::kMuted);
    totals_ = label(CRect(24, 330, 776, 358), "", palette::kMuted);
    apply_ = button(CRect(580, 380, 776, 416), 10, "Apply and reload");
    message_ = makeOwned<CMultiLineTextLabel>(CRect(24, 436, 776, 514));
    message_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    message_->setFontColor(palette::kMuted);
    message_->setBackColor(palette::kCanvas);
    message_->setFrameColor(palette::kCanvas);
    message_->setHoriAlign(kLeftText);
    message_->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
    addRetainedView(*this, message_);
    timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { tick(); }, 50, true);
    updateEnabled();
    inspect(false);
  }
  ~AsioPanel() noexcept override { if (timer_) timer_->stop(); }
  void valueChanged(CControl* source) override {
    if (rendering_ || applying_ || awaitingReload_) return;
    if (source == devices_.get()) { selected_ = devices_->getCurrentIndex(); renderSelected(); return; }
    auto item = draft_.at(selected_);
    if (source == name_.get() || source == inputs_.get() || source == outputs_.get() || source == enabled_.get()) {
      if (!item) return;
      item->device.registry_name = name_->getText().getString();
      item->inputs = inputs_->getText().getString();
      item->outputs = outputs_->getText().getString();
      item->device.enabled = enabled_->getValue() > 0.5F;
      draft_.changed();
      renderList();
      return;
    }
    if (source->getValue() <= 0.5F) return;
    if (source == refresh_.get()) inspect(false);
    else if (source == reload_.get()) {
      if (!draft_.dirty() || confirm(L"Discard unsaved Virtual ASIO edits and read the current engine topology?")) inspect(true);
    } else if (source == add_.get()) {
      GUID uuid{};
      wchar_t value[40]{};
      if (FAILED(CoCreateGuid(&uuid)) || StringFromGUID2(uuid, value, 40) != 39) {
        message("Could not generate a device identity.", true); return;
      }
      std::string token;
      for (int i = 1; i < 37; ++i) token.push_back(static_cast<char>(value[i]));
      if (!draft_.add(token)) { message("Device limit reached or generated identity was rejected.", true); return; }
      selected_ = static_cast<int>(draft_.items().size() - 1);
      renderList(); renderSelected();
    } else if (source == remove_.get()) {
      if (item && confirm(L"Remove this device from the draft? It remains registered until Apply. Routes using removed channels may be lost after applying.") && draft_.remove(selected_)) {
        selected_ = (std::min)(selected_, static_cast<int>(draft_.items().size() - 1));
        renderList(); renderSelected();
      }
    } else if (source == apply_.get()) {
      if (pending_.valid() || !snapshot_.available || snapshot_.running || snapshot_.activeClients || conflict_ || uncertain_) return;
      std::string error;
      auto devices = draft_.build(error);
      if (!devices) { message(error, true); return; }
      if (!confirm(L"Apply the Virtual ASIO names, enabled states and channel counts? All DAWs must be closed. The stopped engine will reload its current session; routes using removed channels may be removed.")) return;
      expected_ = *devices;
      applying_ = true;
      message("Applying Virtual ASIO topology...", false);
      launch([client = client_, devices = std::move(*devices)]() mutable {
        return client->reloadVirtualAsioDevices(std::move(devices));
      });
    }
  }
 private:
  SharedPointer<CTextLabel> label(const CRect& rect, const char* title, CColor color = palette::kText) {
    auto view = makeOwned<CTextLabel>(rect, title);
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13)); view->setFontColor(color);
    view->setBackColor(palette::kCanvas); view->setFrameColor(palette::kCanvas);
    view->setHoriAlign(kLeftText); view->setTextTruncateMode(CTextLabel::kTruncateTail);
    addRetainedView(*this, view); return view;
  }
  SharedPointer<CTextButton> button(const CRect& rect, int tag, const char* title) {
    auto view = makeOwned<CTextButton>(rect, this, tag, title);
    stylePanelButton(*view); view->setTextColor(palette::kText);
    addRetainedView(*this, view); return view;
  }
  SharedPointer<CTextEdit> edit(const CRect& rect, int tag) {
    auto view = makeOwned<CTextEdit>(rect, this, tag, "");
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13)); view->setFontColor(palette::kText);
    view->setBackColor(palette::kSurface); view->setFrameColor(palette::kLine);
    view->setHoriAlign(kLeftText); view->setImmediateTextChange(true);
    addRetainedView(*this, view); return view;
  }
  bool confirm(const wchar_t* text) const {
    auto frame = getFrame();
    if (!frame || !frame->getPlatformFrame()) return false;
    auto native = static_cast<HWND>(frame->getPlatformFrame()->getPlatformRepresentation());
    auto owner = native ? GetAncestor(native, GA_ROOT) : nullptr;
    return owner && MessageBoxW(owner, text, L"Virtual ASIO devices",
        MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
  }
  void message(const std::string& text, bool error) {
    message_->setText(text.c_str()); message_->setTooltipText(text.c_str());
    message_->setFontColor(error ? palette::kDanger : palette::kMuted);
  }
  void renderList() {
    rendering_ = true;
    devices_->removeAllEntry();
    for (const auto& item : draft_.items()) devices_->addEntry(
        (item.device.registry_name + (item.device.enabled ? "" : " [disabled]")).c_str());
    if (draft_.items().empty()) devices_->addEntry("No topology available");
    devices_->setCurrent(selected_ >= 0 ? selected_ : 0);
    rendering_ = false;
    updateEnabled();
  }
  void renderSelected() {
    rendering_ = true;
    auto item = draft_.at(selected_);
    name_->setText(item ? item->device.registry_name.c_str() : "");
    inputs_->setText(item ? item->inputs.c_str() : "");
    outputs_->setText(item ? item->outputs.c_str() : "");
    enabled_->setValue(item && item->device.enabled ? 1.0F : 0.0F); enabled_->invalid();
    const auto id = item ? item->device.device_id + " / " + item->device.clsid : std::string{};
    identity_->setText(id.c_str()); identity_->setTooltipText(id.c_str());
    rendering_ = false; updateEnabled();
  }
  void updateEnabled() {
    const bool idle = !pending_.valid() && !awaitingReload_;
    const bool editable = !applying_ && !awaitingReload_ && snapshot_.available && !conflict_ && !uncertain_;
    const bool selected = editable && draft_.at(selected_);
    setPanelControlEnabled(*refresh_, idle);
    setPanelControlEnabled(*reload_, idle && snapshot_.available);
    setPanelControlEnabled(*devices_, !applying_ && !awaitingReload_ && !draft_.items().empty());
    setPanelControlEnabled(*add_, editable && draft_.items().size() < control::kMaximumVirtualAsioDevices);
    setPanelControlEnabled(*remove_, selected && draft_.items().size() > 1);
    for (CView* view : std::initializer_list<CView*>{name_.get(), inputs_.get(), outputs_.get(), enabled_.get()})
      setPanelControlEnabled(*view, selected);
    std::string error;
    auto topology = draft_.build(error);
    setPanelControlEnabled(*apply_, idle && editable && draft_.dirty() && topology.has_value() &&
                           !snapshot_.running && !snapshot_.activeClients);
    std::uint64_t inputs = 0, outputs = 0;
    if (topology) for (const auto& device : *topology) if (device.enabled) {
      inputs += device.input_channels; outputs += device.output_channels;
    }
    totals_->setText(topology ? ("Enabled totals: " + std::to_string(inputs) + " DAW inputs / " +
        std::to_string(outputs) + " DAW outputs").c_str() : error.c_str());
    totals_->setFontColor(topology ? palette::kMuted : palette::kWarning);
    const auto state = !snapshot_.available ? "Engine unavailable" : snapshot_.running ? "Stop audio before applying" :
        snapshot_.activeClients ? "Close DAW clients before applying" : "Audio stopped";
    status_->setText((std::string(state) + " / " + std::to_string(draft_.items().size()) + " devices" +
        (draft_.dirty() ? " / Unsaved changes" : "") + (awaitingReload_ ? " / Reloading" : "")).c_str());
  }
  template<class Work> void launch(Work work) {
    std::packaged_task<AsioDeviceSnapshot()> task(std::move(work)); auto future = task.get_future();
    try { std::thread(std::move(task)).detach(); pending_ = std::move(future); }
    catch (...) { applying_ = false; message("Could not start ASIO request worker.", true); }
    updateEnabled();
  }
  void inspect(bool discard) {
    if (pending_.valid()) return;
    discardOnRead_ = discard;
    launch([client = client_] { return client->inspectVirtualAsioDevices(); });
  }
  void tick() {
    const auto now = Clock::now();
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      AsioDeviceSnapshot result;
      try { result = pending_.get(); } catch (...) { result.error = "ASIO request failed; refresh before retrying."; }
      nextPoll_ = now + std::chrono::seconds(2);
      if (applying_) {
        applying_ = false;
        if (result.reloadAccepted && result.devices == expected_) {
          awaitingReload_ = true; deadline_ = now + std::chrono::seconds(30);
          nextPoll_ = now + std::chrono::milliseconds(500);
          message("Topology saved; waiting for the engine to reload the same session...", false);
        } else { uncertain_ = true; message(result.error.empty() ? "Topology reply did not match the draft; refresh required." : result.error, true); }
      } else {
        snapshot_ = std::move(result);
        if (snapshot_.available) {
          if (awaitingReload_ && snapshot_.devices == expected_) {
            awaitingReload_ = false; uncertain_ = false; conflict_ = false;
            loadSnapshot(); message("Virtual ASIO topology reloaded and verified.", false);
          } else if (!awaitingReload_) {
            uncertain_ = false;
            if (discardOnRead_ || !draft_.dirty()) {
              if (discardOnRead_ || baseline_ != snapshot_.devices) loadSnapshot();
              conflict_ = false;
            } else if (baseline_ != snapshot_.devices) {
              conflict_ = true; message("Engine topology changed elsewhere. Reload saved before applying this draft.", true);
            }
          }
        } else if (!awaitingReload_) message(snapshot_.error, true);
        discardOnRead_ = false;
      }
      updateEnabled();
    }
    if (awaitingReload_ && now >= deadline_) {
      awaitingReload_ = false; uncertain_ = true;
      message("Topology was saved but reload could not be verified. Refresh; no command was resent.", true);
      updateEnabled();
    }
    if (!pending_.valid() && now >= nextPoll_) inspect(false);
  }
  void loadSnapshot() {
    std::string selectedId;
    if (auto item = draft_.at(selected_)) selectedId = item->device.device_id;
    baseline_ = snapshot_.devices; draft_.load(baseline_);
    selected_ = baseline_.empty() ? -1 : 0;
    for (std::size_t i = 0; i < baseline_.size(); ++i)
      if (baseline_[i].device_id == selectedId) selected_ = static_cast<int>(i);
    renderList(); renderSelected();
  }
  AsioTopologyDraft draft_;
  AsioDeviceSnapshot snapshot_;
  std::vector<control::VirtualAsioDeviceDefinition> baseline_, expected_;
  int selected_ = -1;
  bool rendering_ = false, discardOnRead_ = false, applying_ = false;
  bool awaitingReload_ = false, uncertain_ = false, conflict_ = false;
  Clock::time_point nextPoll_ = Clock::now(), deadline_{};
  std::shared_ptr<EngineClient> client_ = std::make_shared<EngineClient>();
  std::future<AsioDeviceSnapshot> pending_;
  SharedPointer<CVSTGUITimer> timer_;
  SharedPointer<COptionMenu> devices_;
  SharedPointer<CTextEdit> name_, inputs_, outputs_;
  SharedPointer<CCheckBox> enabled_;
  SharedPointer<CTextButton> refresh_, add_, remove_, reload_, apply_;
  SharedPointer<CTextLabel> status_, identity_, totals_;
  SharedPointer<CMultiLineTextLabel> message_;
};
struct LastWindow final : WindowListenerAdapter {
  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) IApplication::instance().quit();
  }
} lastWindow;
}  // namespace

void showAsioDeviceWindow() {
  for (const auto& window : IApplication::instance().getWindows())
    if (window->getTitle().getString() == kTitle) { window->show(); window->activate(); return; }
  WindowConfiguration config; config.type = WindowType::Document;
  config.style.border().close().size(); config.size = CPoint(kWidth, kHeight); config.title = kTitle;
  auto window = IApplication::instance().createWindow(config, std::make_shared<WindowControllerAdapter>());
  if (!window) return;
  auto frame = makeOwned<CFrame>(CRect(0, 0, kWidth, kHeight), nullptr);
  frame->setFocusDrawingEnabled(true); frame->setFocusColor(palette::kSteel);
  auto viewport = makeOwned<ThemedScrollView>(CRect(0, 0, kWidth, kHeight), CRect(0, 0, kWidth, kHeight),
      CScrollView::kHorizontalScrollbar | CScrollView::kVerticalScrollbar | CScrollView::kAutoHideScrollbars |
      CScrollView::kDontDrawFrame | CScrollView::kFollowFocusView);
  viewport->setBackgroundColor(palette::kCanvas); styleScrollbars(*viewport);
  viewport->setAutosizeFlags(kAutosizeLeft | kAutosizeTop | kAutosizeRight | kAutosizeBottom);
  addRetainedView(*viewport, makeOwned<AsioPanel>()); addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWidth, kHeight),
      MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST));
  window->registerWindowListener(&lastWindow); window->show();
}
}  // namespace sar::gui_vstgui
