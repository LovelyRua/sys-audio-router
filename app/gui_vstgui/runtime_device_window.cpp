#include "app/gui_vstgui/runtime_device_window.h"
#include "app/gui_vstgui/runtime_device_model.h"
#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/view_ownership.h"
#include "app/gui_vstgui/window_geometry.h"
#include "app/gui_vstgui/vb_cable_setup.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

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
#include "vstgui/standalone/include/helpers/windowcontroller.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"

#include <chrono>
#include <future>
#include <memory>
#include <thread>

namespace sar::gui_vstgui {
namespace {
using namespace VSTGUI;
using namespace VSTGUI::Standalone;
constexpr const char* kTitle = "Audio devices";
constexpr CCoord kWidth = 860, kHeight = 650;

class RuntimePanel final : public CViewContainer, public IControlListener {
 public:
  RuntimePanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    text(CRect(24, 16, 330, 46), "AUDIO DEVICES", palette::kSteel);
    refresh_ = button(CRect(732, 16, 836, 46), "Refresh", 1);
    status_ = text(CRect(24, 54, 836, 82), "Reading runtime...");
    text(CRect(24, 96, 150, 126), "Matrix endpoint", palette::kMuted);
    endpoints_ = menu(CRect(160, 96, 836, 128), 2);
    addCapture_ = button(CRect(160, 142, 308, 174), "+ Capture", 3);
    addRender_ = button(CRect(320, 142, 468, 174), "+ Playback", 4);
    remove_ = button(CRect(688, 142, 836, 174), "Remove endpoint", 5);
    reload_ = button(CRect(480, 142, 672, 174), "Reload saved", 14);
    text(CRect(24, 194, 150, 226), "Endpoint ID", palette::kMuted);
    id_ = edit(CRect(160, 194, 596, 226), 6);
    master_ = button(CRect(608, 194, 836, 226), "Set clock master", 7);
    text(CRect(24, 244, 150, 276), "WASAPI device", palette::kMuted);
    devices_ = menu(CRect(160, 244, 836, 276), 8);
    text(CRect(24, 294, 150, 326), "First channel", palette::kMuted);
    first_ = edit(CRect(160, 294, 280, 326), 9);
    text(CRect(320, 294, 462, 326), "Channel count", palette::kMuted);
    channels_ = edit(CRect(470, 294, 590, 326), 10);
    details_ = text(CRect(160, 338, 836, 366), "", palette::kMuted);
    reset_ = button(CRect(24, 384, 172, 420), "New matrix", 11);
    run_ = button(CRect(464, 384, 636, 420), "Start audio", 12);
    apply_ = button(CRect(648, 384, 836, 420), "Apply", 13);
    cableSetup_ = button(CRect(24, 440, 232, 476), "Install VB-CABLE...", 15);
    cableWebsite_ = button(CRect(248, 440, 460, 476), "VB-Audio / Donate", 16);
    text(CRect(24, 482, 836, 510), "VB-CABLE by VB-Audio (vb-cable.com). Donationware; contributions are welcome.", palette::kMuted);
    message_ = makeOwned<CMultiLineTextLabel>(CRect(24, 522, 836, 626));
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

  ~RuntimePanel() noexcept override { timer_->stop(); }

  void valueChanged(CControl* source) override {
    SharedPointer<RuntimePanel> keepAlive(this);
    if (rendering_ || pending_.valid() || setup_.valid()) return;
    if (source == cableWebsite_.get() && source->getValue() > 0.5F) {
      ShellExecuteW(nullptr, L"open", L"https://vb-audio.com/Cable/", nullptr, nullptr, SW_SHOWNORMAL);
      return;
    }
    if (source == cableSetup_.get() && source->getValue() > 0.5F) {
      if (!confirm(L"Open the original VB-Audio installer? VB-CABLE is donationware from vb-cable.com. Review its terms and complete installation yourself. A reboot may be required. Existing cable routes may be interrupted.")) return;
      if (!getFrame()) return;
      std::packaged_task<VbCableSetupResult()> task([] { return openVbCableSetup(); });
      auto future = task.get_future();
      try {
        std::thread(std::move(task)).detach();
        setup_ = std::move(future);
        message("Opening original VB-CABLE setup...");
      } catch (...) { message("Could not start setup worker", true); }
      updateEnabled();
      return;
    }
    if (source == endpoints_.get()) {
      selected_ = endpoints_->getCurrentIndex();
      renderSelected();
      return;
    }
    auto item = selectedItem();
    if (source == id_.get() || source == first_.get() || source == channels_.get()) {
      if (item && editableItem()) {
        item->endpoint.endpoint_id = id_->getText().getString();
        item->firstChannel = first_->getText().getString();
        item->channelCount = channels_->getText().getString();
        draft_.changed();
        updateEnabled();
      }
      return;
    }
    if (source == devices_.get()) {
      const auto index = devices_->getCurrentIndex();
      if (item && editableItem() && index >= 0 && static_cast<std::size_t>(index) < deviceIds_.size()) {
        item->endpoint.device_id = deviceIds_[static_cast<std::size_t>(index)];
        draft_.changed();
        updateEnabled();
      }
      return;
    }
    if (source->getValue() <= 0.5F) return;
    if (source == refresh_.get()) {
      inspect();
    } else if (source == reload_.get()) {
      if (!draft_.dirty() || confirm(L"Discard unsaved endpoint changes and reload the engine configuration?"))
        if (getFrame()) inspect(true);
    } else if (source == reset_.get()) {
      if (confirm(L"Create a new empty WASAPI matrix draft? Existing configuration is not changed until Apply is requested.") && getFrame()) {
        draft_.newMatrix(); selected_ = -1; renderEndpoints();
      }
    } else if (source == addCapture_.get() || source == addRender_.get()) {
      if (draft_.add(source == addCapture_.get() ? control::AudioRuntimeEndpointDirection::Capture
                                                : control::AudioRuntimeEndpointDirection::Render)) {
        selected_ = static_cast<int>(draft_.endpoints().size() - 1);
        renderEndpoints();
      }
    } else if (source == remove_.get()) {
      if (selected_ >= 0 && draft_.remove(static_cast<std::size_t>(selected_))) {
        selected_ = draft_.endpoints().empty() ? -1 : (std::min)(selected_, static_cast<int>(draft_.endpoints().size() - 1));
        renderEndpoints();
      }
    } else if (source == master_.get()) {
      if (selected_ >= 0 && draft_.makeMaster(static_cast<std::size_t>(selected_))) renderEndpoints();
    } else if (source == apply_.get()) {
      std::string error;
      auto configuration = draft_.build(snapshot_.devices, error);
      if (!configuration) { message(error, true); return; }
      message("Applying runtime configuration...");
      reloadOnCompletion_ = true;
      launch([client = client_, configuration = std::move(*configuration)]() mutable {
        return client->configureDevices(std::move(configuration));
      });
    } else if (source == run_.get()) {
      reloadOnCompletion_ = false;
      message(snapshot_.running ? "Stopping audio..." : "Starting audio...");
      launch([client = client_, running = !snapshot_.running] { return client->setRuntimeRunning(running); });
    }
    updateEnabled();
  }

 private:
  SharedPointer<CTextLabel> text(const CRect& bounds, const char* value,
                                CColor color = palette::kText) {
    auto view = makeOwned<CTextLabel>(bounds, value);
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    view->setFontColor(color);
    view->setBackColor(palette::kCanvas);
    view->setFrameColor(palette::kCanvas);
    view->setHoriAlign(kLeftText);
    addRetainedView(*this, view);
    return view;
  }
  SharedPointer<CTextButton> button(const CRect& bounds, const char* value, int tag) {
    auto view = makeOwned<CTextButton>(bounds, this, tag, value);
    view->setGradient(nullptr);
    view->setTextColor(palette::kText);
    view->setFrameColor(palette::kLine);
    view->setRoundRadius(2);
    addRetainedView(*this, view);
    return view;
  }
  SharedPointer<CTextEdit> edit(const CRect& bounds, int tag) {
    auto view = makeOwned<CTextEdit>(bounds, this, tag, "");
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    view->setFontColor(palette::kText);
    view->setBackColor(palette::kRaised);
    view->setFrameColor(palette::kLine);
    view->setHoriAlign(kLeftText);
    view->setImmediateTextChange(true);
    addRetainedView(*this, view);
    return view;
  }
  SharedPointer<COptionMenu> menu(const CRect& bounds, int tag) {
    auto view = makeOwned<COptionMenu>(bounds, this, tag);
    view->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    view->setFontColor(palette::kText);
    view->setBackColor(palette::kRaised);
    view->setFrameColor(palette::kLine);
    addRetainedView(*this, view);
    return view;
  }
  bool confirm(const wchar_t* value) const {
    const auto frame = getFrame();
    if (!frame || !frame->getPlatformFrame()) return false;
    const auto native = static_cast<HWND>(frame->getPlatformFrame()->getPlatformRepresentation());
    const auto owner = native ? GetAncestor(native, GA_ROOT) : nullptr;
    return owner && MessageBoxW(owner, value, L"Audio devices", MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
  }
  void message(const std::string& value, bool error = false) {
    message_->setFontColor(error ? palette::kDanger : palette::kMuted);
    message_->setText(value.c_str());
  }
  RuntimeEndpointDraft* selectedItem() {
    return selected_ < 0 ? nullptr : draft_.at(static_cast<std::size_t>(selected_));
  }
  bool editableItem() {
    const auto item = selectedItem();
    return draft_.editable() && item && item->endpoint.backend == control::AudioRuntimeEndpointBackend::Wasapi;
  }
  template <class Work> void launch(Work work) {
    std::packaged_task<RuntimeDeviceSnapshot()> task(std::move(work));
    auto future = task.get_future();
    try { std::thread(std::move(task)).detach(); pending_ = std::move(future); }
    catch (...) { message("Could not start runtime request worker", true); }
    updateEnabled();
  }
  void inspect(bool reload = false) {
    reloadOnCompletion_ = reload || !draft_.dirty();
    message("Reading engine configuration and device inventory...");
    launch([client = client_] { return client->inspectDevices(); });
  }
  void complete() {
    if (setup_.valid() && setup_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try {
        const auto result = setup_.get();
        message(result.message, result.failed);
      } catch (...) { message("Vendor setup request failed. No automatic retry.", true); }
      updateEnabled();
    }
    if (!pending_.valid() || pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
      auto result = pending_.get();
      const bool accepted = result.available && result.operationSucceeded;
      if (accepted && reloadOnCompletion_) draft_.load(result.configuration);
      snapshot_ = std::move(result);
      message(snapshot_.error, !snapshot_.error.empty());
      renderEndpoints();
    } catch (...) {
      snapshot_.available = false;
      message("Runtime request failed; draft retained. Refresh to read actual state.", true);
    }
    updateEnabled();
  }
  void renderEndpoints() {
    rendering_ = true;
    endpoints_->removeAllEntry();
    for (const auto& item : draft_.endpoints()) {
      const auto name = item.endpoint.endpoint_id +
          (item.endpoint.direction == control::AudioRuntimeEndpointDirection::Capture ? " / capture" : " / playback") +
          (item.endpoint.clock_master ? " / MASTER" : "");
      endpoints_->addEntry(name.c_str());
    }
    if (draft_.endpoints().empty()) {
      endpoints_->addEntry("No configured endpoints"); selected_ = -1;
    } else if (selected_ < 0 || static_cast<std::size_t>(selected_) >= draft_.endpoints().size()) selected_ = 0;
    endpoints_->setCurrent(selected_ < 0 ? 0 : selected_);
    rendering_ = false;
    renderSelected();
  }
  void renderSelected() {
    rendering_ = true;
    const auto item = selectedItem();
    id_->setText(item ? item->endpoint.endpoint_id.c_str() : "");
    first_->setText(item ? item->firstChannel.c_str() : "");
    channels_->setText(item ? item->channelCount.c_str() : "");
    devices_->removeAllEntry();
    deviceIds_.clear();
    int selected = -1;
    if (item && editableItem()) {
      for (const auto& device : snapshot_.devices) {
        if (!RuntimeDeviceDraft::matches(device, item->endpoint.direction)) continue;
        if (device.id == item->endpoint.device_id) selected = static_cast<int>(deviceIds_.size());
        devices_->addEntry((device.label + (device.is_default ? " / default" : "")).c_str());
        deviceIds_.push_back(device.id);
      }
    }
    if (selected < 0) {
      devices_->addEntry(item && !item->endpoint.device_id.empty() ? "Configured binding unavailable / preserved" : "Select a device");
      deviceIds_.push_back(item ? item->endpoint.device_id : std::string{});
      selected = static_cast<int>(deviceIds_.size() - 1);
    }
    devices_->setCurrent(selected);
    details_->setText(item && !editableItem() ? "Physical ASIO endpoint preserved (read-only)" : "");
    master_->setTitle(item && item->endpoint.clock_master ? "Clock master" : "Set clock master");
    rendering_ = false;
    updateEnabled();
  }
  void updateEnabled() {
    const auto enable = [](CView* view, bool active) {
      view->setMouseEnabled(active); view->setAlphaValue(active ? 1.0F : 0.4F);
    };
    const bool idle = !pending_.valid() && !setup_.valid(), available = snapshot_.available && idle;
    const auto item = selectedItem();
    const bool edit = available && editableItem();
    enable(refresh_, idle);
    enable(cableSetup_, idle);
    enable(cableWebsite_, idle);
    enable(reload_, idle);
    enable(endpoints_, available && !draft_.endpoints().empty());
    enable(addCapture_, available && draft_.editable() && draft_.endpoints().size() < control::kMaximumAudioRuntimeEndpoints);
    enable(addRender_, available && draft_.editable() && draft_.endpoints().size() < control::kMaximumAudioRuntimeEndpoints);
    enable(reset_, available);
    enable(remove_, edit);
    enable(id_, edit); enable(first_, edit); enable(channels_, edit); enable(devices_, edit);
    enable(master_, edit && item && !item->endpoint.clock_master && selected_ >= 0 &&
        draft_.canMakeMaster(static_cast<std::size_t>(selected_)));
    enable(apply_, available && draft_.editable() && draft_.dirty());
    enable(run_, available && !draft_.dirty() && snapshot_.configuration.mode != control::AudioRuntimeMode::None);
    run_->setTitle(snapshot_.running ? "Stop audio" : "Start audio");
    const auto status = !snapshot_.available ? std::string("Engine unavailable")
        : !draft_.editable() ? std::string("Existing runtime preserved / read-only")
        : (snapshot_.running ? std::string("Audio running") : std::string("Audio stopped")) +
          " / " + std::to_string(draft_.endpoints().size()) + " endpoints" +
          (draft_.dirty() ? " / Unsaved changes" : "");
    status_->setText(status.c_str());
  }

  RuntimeDeviceSnapshot snapshot_;
  RuntimeDeviceDraft draft_;
  int selected_ = -1;
  bool rendering_ = false;
  bool reloadOnCompletion_ = true;
  std::vector<std::string> deviceIds_;
  std::shared_ptr<EngineClient> client_ = std::make_shared<EngineClient>();
  std::future<RuntimeDeviceSnapshot> pending_;
  std::future<VbCableSetupResult> setup_;
  SharedPointer<CVSTGUITimer> timer_;
  SharedPointer<COptionMenu> endpoints_, devices_;
  SharedPointer<CTextEdit> id_, first_, channels_;
  SharedPointer<CTextLabel> status_, details_;
  SharedPointer<CMultiLineTextLabel> message_;
  SharedPointer<CTextButton> refresh_, reload_, addCapture_, addRender_, remove_, master_, reset_, run_, apply_;
  SharedPointer<CTextButton> cableSetup_, cableWebsite_;
};

struct LastRuntimeWindow final : WindowListenerAdapter {
  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) IApplication::instance().quit();
  }
};
LastRuntimeWindow lastWindow;
}

void showRuntimeDeviceWindow() {
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
  addRetainedView(*viewport, makeOwned<RuntimePanel>());
  addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWidth, kHeight),
      MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST));
  window->registerWindowListener(&lastWindow);
  window->show();
}
}
