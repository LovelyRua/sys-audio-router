#include "app/gui_vstgui/runtime_device_window.h"
#include "app/gui_vstgui/localization.h"
#include "app/gui_vstgui/runtime_device_model.h"
#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/panel_button.h"
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
constexpr CCoord kWidth = 860, kHeight = 690;

class RuntimePanel final : public CViewContainer, public IControlListener {
 public:
  RuntimePanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    text(CRect(24, 16, 330, 46), tr("AUDIO DEVICES"), palette::kSteel);
    refresh_ = button(CRect(732, 16, 836, 46), tr("Refresh"), 1);
    status_ = text(CRect(24, 54, 836, 82), tr("Reading runtime..."));
    text(CRect(24, 96, 150, 126), tr("Matrix endpoint"), palette::kMuted);
    endpoints_ = menu(CRect(160, 96, 836, 128), 2);
    addCapture_ = button(CRect(160, 142, 308, 174), tr("+ Capture"), 3);
    addRender_ = button(CRect(320, 142, 468, 174), tr("+ Playback"), 4);
    addAsio_ = button(CRect(24, 142, 148, 174), tr("+ ASIO"), 17);
    remove_ = button(CRect(688, 142, 836, 174), tr("Remove endpoint"), 5);
    reload_ = button(CRect(480, 142, 672, 174), tr("Reload saved"), 14);
    text(CRect(24, 194, 150, 226), tr("Endpoint ID"), palette::kMuted);
    id_ = edit(CRect(160, 194, 596, 226), 6);
    master_ = button(CRect(608, 194, 836, 226), tr("Set clock master"), 7);
    text(CRect(24, 244, 150, 276), tr("Audio device"), palette::kMuted);
    devices_ = menu(CRect(160, 244, 836, 276), 8);
    text(CRect(24, 294, 150, 326), tr("First channel"), palette::kMuted);
    first_ = edit(CRect(160, 294, 280, 326), 9);
    text(CRect(320, 294, 462, 326), tr("Channel count"), palette::kMuted);
    channels_ = edit(CRect(470, 294, 590, 326), 10);
    text(CRect(24, 338, 150, 370), tr("ASIO rate (Hz)"), palette::kMuted);
    rate_ = edit(CRect(160, 338, 280, 370), 18);
    text(CRect(320, 338, 462, 370), tr("Buffer (frames)"), palette::kMuted);
    frames_ = edit(CRect(470, 338, 590, 370), 19);
    details_ = text(CRect(160, 376, 836, 404), "", palette::kMuted);
    reset_ = button(CRect(24, 424, 172, 460), tr("New matrix"), 11);
    run_ = button(CRect(464, 424, 636, 460), tr("Start audio"), 12);
    apply_ = button(CRect(648, 424, 836, 460), tr("Apply"), 13);
    cableSetup_ = button(CRect(24, 480, 232, 516), tr("Install VB-CABLE..."), 15);
    cableWebsite_ = button(CRect(248, 480, 460, 516), tr("VB-Audio / Donate"), 16);
    text(CRect(24, 522, 836, 550), tr("VB-CABLE by VB-Audio (vb-cable.com). Donationware; contributions are welcome."), palette::kMuted);
    message_ = makeOwned<CMultiLineTextLabel>(CRect(24, 562, 836, 666));
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
    if (source == rate_.get() || source == frames_.get()) {
      if (selected_ >= 0) draft_.setTiming(static_cast<std::size_t>(selected_),
          rate_->getText().getString(), frames_->getText().getString());
      updateEnabled();
      return;
    }
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
      if (item && draft_.editable() && index >= 0 && static_cast<std::size_t>(index) < deviceIds_.size()) {
        draft_.bindDevice(static_cast<std::size_t>(selected_), deviceIds_[static_cast<std::size_t>(index)]);
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
    } else if (source == addAsio_.get()) {
      if (draft_.addPhysicalAsio()) {
        selected_ = static_cast<int>(draft_.endpoints().size() - 2);
        renderEndpoints();
      }
    } else if (source == remove_.get()) {
      const bool physical = item && item->endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio;
      if (physical && !confirm(L"Remove both Physical ASIO endpoints from this draft? Routes using their channels may be removed after Apply. Remaining WASAPI endpoints are retained.")) return;
      if (!getFrame()) return;
      if (selected_ >= 0 && (physical ? draft_.removePhysicalAsio(static_cast<std::size_t>(selected_))
                                    : draft_.remove(static_cast<std::size_t>(selected_)))) {
        selected_ = draft_.endpoints().empty() ? -1 : (std::min)(selected_, static_cast<int>(draft_.endpoints().size() - 1));
        renderEndpoints();
      }
    } else if (source == master_.get()) {
      if (selected_ >= 0 && draft_.makeMaster(static_cast<std::size_t>(selected_))) renderEndpoints();
    } else if (source == apply_.get()) {
      std::string error;
      auto configuration = draft_.build(snapshot_.devices, error);
      if (!configuration) { message(error, true); return; }
      if ((draft_.hasPhysicalAsio() || std::any_of(snapshot_.configuration.endpoints.begin(),
            snapshot_.configuration.endpoints.end(), [](const auto& endpoint) {
              return endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio;
            })) && !confirm(L"Apply this matrix configuration? The ASIO driver may reopen and audio may be interrupted. Removed channels can lose routes. Hardware must support the requested channel ranges, sample rate and buffer size.")) return;
      if (!getFrame()) return;
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
    stylePanelButton(*view);
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
    return owner && MessageBoxW(owner, trWide(value).c_str(), trWide(L"Audio devices").c_str(), MB_YESNO | MB_DEFBUTTON2 | MB_ICONWARNING) == IDYES;
  }
  void message(const std::string& value, bool error = false) {
    message_->setFontColor(error ? palette::kDanger : palette::kMuted);
    message_->setText(trText(value).c_str());
  }
  RuntimeEndpointDraft* selectedItem() {
    return selected_ < 0 ? nullptr : draft_.at(static_cast<std::size_t>(selected_));
  }
  bool editableItem() {
    const auto item = selectedItem();
    return draft_.editable() && item;
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
          (item.endpoint.direction == control::AudioRuntimeEndpointDirection::Capture ? tr(" / capture") : tr(" / playback")) +
          (item.endpoint.clock_master ? tr(" / MASTER") : "");
      endpoints_->addEntry(name.c_str());
    }
    if (draft_.endpoints().empty()) {
      endpoints_->addEntry(tr("No configured endpoints")); selected_ = -1;
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
    const bool physical = item && item->endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio;
    rate_->setText(physical ? item->sampleRate.c_str() : "");
    frames_->setText(physical ? item->blockFrames.c_str() : "");
    devices_->removeAllEntry();
    deviceIds_.clear();
    int selected = -1;
    if (item && draft_.editable()) {
      for (const auto& device : snapshot_.devices) {
        if (!RuntimeDeviceDraft::matchesEndpoint(device, item->endpoint)) continue;
        if (device.id == item->endpoint.device_id) selected = static_cast<int>(deviceIds_.size());
        devices_->addEntry((device.label + (device.is_default ? tr(" / default") : "")).c_str());
        deviceIds_.push_back(device.id);
      }
    }
    if (selected < 0) {
      devices_->addEntry(item && !item->endpoint.device_id.empty() ? tr("Configured binding unavailable / preserved") : tr("Select a device"));
      deviceIds_.push_back(item ? item->endpoint.device_id : std::string{});
      selected = static_cast<int>(deviceIds_.size() - 1);
    }
    devices_->setCurrent(selected);
    details_->setText(physical ? tr("ASIO group / master clock / native channel offset is zero-based") : tr("Native channel offset is zero-based"));
    remove_->setTitle(physical ? tr("Remove ASIO group") : tr("Remove endpoint"));
    master_->setTitle(item && item->endpoint.clock_master ? tr("Clock master") : tr("Set clock master"));
    rendering_ = false;
    updateEnabled();
  }
  void updateEnabled() {
    const auto enable = [](CView* view, bool active) {
      setPanelControlEnabled(*view, active);
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
    enable(addAsio_, available && draft_.editable() && !draft_.hasPhysicalAsio() &&
        draft_.endpoints().size() + 2 <= control::kMaximumAudioRuntimeEndpoints);
    enable(reset_, available);
    enable(remove_, edit);
    enable(id_, edit); enable(first_, edit); enable(channels_, edit);
    enable(devices_, available && draft_.editable() && item);
    const bool physical = item && item->endpoint.backend == control::AudioRuntimeEndpointBackend::PhysicalAsio;
    enable(rate_, edit && physical); enable(frames_, edit && physical);
    enable(master_, edit && item && !item->endpoint.clock_master && selected_ >= 0 &&
        draft_.canMakeMaster(static_cast<std::size_t>(selected_)));
    std::string validationError;
    const bool valid = draft_.editable() && draft_.build(snapshot_.devices, validationError).has_value();
    enable(apply_, available && draft_.dirty() && valid);
    if (available && draft_.dirty()) details_->setText(valid ? tr("Ready to apply") : trText(validationError).c_str());
    enable(run_, available && !draft_.dirty() && snapshot_.configuration.mode != control::AudioRuntimeMode::None);
    run_->setTitle(snapshot_.running ? tr("Stop audio") : tr("Start audio"));
    const auto status = !snapshot_.available ? std::string(tr("Engine unavailable"))
        : !draft_.editable() ? std::string(tr("Existing runtime preserved / read-only"))
        : (snapshot_.running ? std::string(tr("Audio running")) : std::string(tr("Audio stopped"))) +
          " / " + std::to_string(draft_.endpoints().size()) + tr(" endpoints") +
          (draft_.dirty() ? tr(" / Unsaved changes") : "");
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
  SharedPointer<CTextEdit> id_, first_, channels_, rate_, frames_;
  SharedPointer<CTextLabel> status_, details_;
  SharedPointer<CMultiLineTextLabel> message_;
  SharedPointer<CTextButton> refresh_, reload_, addCapture_, addRender_, remove_, master_, reset_, run_, apply_;
  SharedPointer<CTextButton> cableSetup_, cableWebsite_;
  SharedPointer<CTextButton> addAsio_;
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
