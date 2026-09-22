#include "app/gui_vstgui/main_window.h"

#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/lcd_readout_view.h"
#include "app/gui_vstgui/palette.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"

#include <cstdio>
#include <memory>

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

constexpr CCoord kWindowWidth = 1000;
constexpr CCoord kWindowHeight = 640;
constexpr CCoord kHeaderHeight = 54;
constexpr std::uint32_t kPollIntervalMs = 750;

// A small filled circle: the header's connection indicator.
class StatusDotView final : public CView {
 public:
  explicit StatusDotView(const CRect& size) : CView(size), color_(palette::kDanger) {}

  void setColor(CColor color) {
    if (color_ == color) {
      return;
    }
    color_ = color;
    invalid();
  }

  void draw(CDrawContext* context) override {
    context->setDrawMode(kAntiAliasing);
    context->setFillColor(color_);
    context->drawEllipse(getViewSize(), kDrawFilled);
  }

 private:
  CColor color_;
};

std::string format_uint(const char* format, std::uint64_t value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

// Mediates between EngineClient (blocking pipe I/O, run synchronously on the
// UI thread's poll timer for this first slice -- see the note in
// docs/product-readiness.md about moving this to a background queue) and the
// header's views. Owned by a shared_ptr captured in the timer and button
// callbacks so it outlives both for the app's lifetime.
class HeaderController final : public IControlListener {
 public:
  void attachViews(SharedPointer<StatusDotView> dot, SharedPointer<CTextLabel> statusText,
                   SharedPointer<LcdReadoutView> hz, SharedPointer<LcdReadoutView> smp,
                   SharedPointer<LcdReadoutView> xrun, SharedPointer<CTextButton> button) {
    dot_ = std::move(dot);
    statusText_ = std::move(statusText);
    hzReadout_ = std::move(hz);
    smpReadout_ = std::move(smp);
    xrunReadout_ = std::move(xrun);
    button_ = std::move(button);
  }

  void refresh() {
    if (busy_) {
      return;
    }
    apply(client_.poll());
  }

  void valueChanged(CControl* control) override {
    if (control != button_.get() || control->getValue() <= 0.5f || busy_) {
      return;
    }
    busy_ = true;
    apply(last_.runtimeRunning ? client_.stop() : client_.start());
    busy_ = false;
  }

 private:
  void apply(const EngineState& state) {
    last_ = state;
    if (dot_) {
      dot_->setColor(state.transportOk ? palette::kHealthy : palette::kDanger);
    }
    if (statusText_) {
      statusText_->setText(state.transportOk ? "Engine online" : "Engine offline");
    }
    if (hzReadout_) {
      hzReadout_->setText(state.sampleRate > 0
                              ? format_uint("%llu HZ", state.sampleRate)
                              : std::string("-- HZ"));
    }
    if (smpReadout_) {
      smpReadout_->setText(state.blockFrames > 0
                               ? format_uint("%llu SMP", state.blockFrames)
                               : std::string("-- SMP"));
    }
    if (xrunReadout_) {
      xrunReadout_->setText(format_uint("XRUN %llu", state.xrunCount));
      xrunReadout_->setTint(state.xrunCount > 0 ? palette::kWarning : palette::kLcdDim);
    }
    if (button_) {
      button_->setTitle(!state.runtimeConfigured ? "Configure audio"
                        : state.runtimeRunning   ? "Stop engine"
                                                  : "Start engine");
      button_->setMouseEnabled(state.transportOk && state.runtimeConfigured);
    }
  }

  EngineClient client_;
  EngineState last_;
  bool busy_ = false;
  SharedPointer<StatusDotView> dot_;
  SharedPointer<CTextLabel> statusText_;
  SharedPointer<LcdReadoutView> hzReadout_;
  SharedPointer<LcdReadoutView> smpReadout_;
  SharedPointer<LcdReadoutView> xrunReadout_;
  SharedPointer<CTextButton> button_;
};

SharedPointer<CTextLabel> makeLabel(const CRect& rect, const char* text, CColor color,
                                    bool bold) {
  auto label = makeOwned<CTextLabel>(rect, text);
  label->setBackColor(CColor(0, 0, 0, 0));
  label->setFrameColor(CColor(0, 0, 0, 0));
  label->setFontColor(color);
  label->setHoriAlign(kLeftText);
  label->setFont(makeOwned<CFontDesc>("Segoe UI", bold ? 14 : 12,
                                      bold ? kBoldFace : 0));
  return label;
}

}  // namespace

WindowPtr createMainWindow() {
  WindowConfiguration config;
  config.type = WindowType::Document;
  config.style.border().close().size().centered();
  config.size = CPoint(kWindowWidth, kWindowHeight);
  config.title = "System Audio Route";
  config.autoSaveFrameName = "SystemAudioRouteMainWindow";

  auto window = IApplication::instance().createWindow(config, nullptr);
  if (!window) {
    return nullptr;
  }

  auto frame = makeOwned<CFrame>(CRect(0, 0, kWindowWidth, kWindowHeight), nullptr);
  frame->setBackgroundColor(palette::kCanvas);

  auto header = makeOwned<CViewContainer>(CRect(0, 0, kWindowWidth, kHeaderHeight));
  header->setBackgroundColor(palette::kSurface);
  frame->addView(header);

  CCoord x = 18;
  auto brand = makeLabel(CRect(x, 0, x + 220, kHeaderHeight), "SYSTEM AUDIO ROUTE",
                         palette::kText, true);
  header->addView(brand);
  x += 236;

  auto dot = makeOwned<StatusDotView>(CRect(x, kHeaderHeight / 2 - 4, x + 8,
                                            kHeaderHeight / 2 + 4));
  header->addView(dot);
  x += 16;

  auto statusText = makeLabel(CRect(x, 0, x + 130, kHeaderHeight), "Engine offline",
                              palette::kText, false);
  header->addView(statusText);

  const CCoord readoutWidth = 96;
  const CCoord readoutHeight = 26;
  const CCoord readoutY = (kHeaderHeight - readoutHeight) / 2;
  CCoord right = kWindowWidth - 18;

  right -= 120;
  auto startStopButton = makeOwned<CTextButton>(
      CRect(right, readoutY, right + 120, readoutY + readoutHeight), nullptr, -1,
      "Configure audio");
  startStopButton->setTextColor(palette::kText);
  startStopButton->setFrameColor(palette::kLine);
  startStopButton->setGradient(nullptr);
  startStopButton->setRoundRadius(2);
  header->addView(startStopButton);

  right -= 10 + readoutWidth;
  auto xrunReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  header->addView(xrunReadout);

  right -= 8 + readoutWidth;
  auto smpReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  header->addView(smpReadout);

  right -= 8 + readoutWidth;
  auto hzReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  header->addView(hzReadout);

  auto body = makeOwned<CViewContainer>(
      CRect(0, kHeaderHeight, kWindowWidth, kWindowHeight));
  body->setBackgroundColor(palette::kCanvas);
  auto placeholder = makeLabel(CRect(24, 20, kWindowWidth - 24, 44),
                               "Routing matrix, devices, and diagnostics are not "
                               "ported to this GUI yet.",
                               palette::kMuted, false);
  body->addView(placeholder);
  frame->addView(body);

  auto controller = std::make_shared<HeaderController>();
  controller->attachViews(dot, statusText, hzReadout, smpReadout, xrunReadout,
                          startStopButton);
  startStopButton->setListener(controller.get());
  // The controller and its views must outlive the timer, which is the case
  // here: the frame (owned by the window) holds the views, and this lambda
  // holds the last strong reference to the controller for the process's
  // lifetime, matching CVSTGUITimer's own fire-and-forget ownership model.
  new CVSTGUITimer([controller](CVSTGUITimer*) { controller->refresh(); },
                   kPollIntervalMs, true);
  controller->refresh();

  window->setContentView(frame);
  return window;
}

}  // namespace sar::gui_vstgui
