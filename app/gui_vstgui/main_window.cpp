#include "app/gui_vstgui/main_window.h"

#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/lcd_readout_view.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/panel_button.h"
#include "app/gui_vstgui/virtual_device_window.h"
#include "app/gui_vstgui/runtime_device_window.h"
#include "app/gui_vstgui/preset_window.h"
#include "app/gui_vstgui/diagnostics_window.h"
#include "app/gui_vstgui/request_error_state.h"
#include "app/gui_vstgui/matrix_page.h"
#include "app/gui_vstgui/route_gain_text.h"
#include "app/gui_vstgui/route_edit_queue.h"
#include "app/gui_vstgui/view_ownership.h"
#include "app/gui_vstgui/window_geometry.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cscrollview.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"

#include <cstdio>
#include <cmath>
#include <chrono>
#include <future>
#include <memory>
#include <vector>
#include <algorithm>
#include <optional>
#include <thread>

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

constexpr CCoord kNavigationWidth = 160;
constexpr CCoord kWorkspaceWidth = 1000;
constexpr CCoord kWindowWidth = kNavigationWidth + kWorkspaceWidth;
constexpr CCoord kWindowHeight = 640;
constexpr CCoord kHeaderHeight = 54;
constexpr std::uint32_t kPollIntervalMs = 750;
constexpr std::size_t kVisibleRows = 8;
constexpr std::size_t kVisibleColumns = 5;

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

class MatrixCellButton final : public CTextButton {
 public:
  using CTextButton::CTextButton;

  CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override {
    if (buttons & kRButton) {
      selectOnly_ = true;
      setValue(1.0F);
      valueChanged();
      setValue(0.0F);
      selectOnly_ = false;
      return kMouseDownEventHandledButDontNeedMovedOrUpEvents;
    }
    return CTextButton::onMouseDown(where, buttons);
  }

  bool selectOnly() const noexcept { return selectOnly_; }

  void present(bool available, bool connected, bool muted, bool selected) {
    const std::string caption = !available ? "" : !connected ? "+" : muted ? "MUTE" : "ON";
    const auto fill = connected ? (muted ? palette::kSurface : CColor(35, 61, 44)) : palette::kRaised;
    const auto border = selected ? palette::kSteel : palette::kLine;
    const auto ink = !connected ? palette::kMuted : muted ? palette::kWarning : palette::kHealthy;
    if (caption_ == caption && fill_ == fill && border_ == border && ink_ == ink) return;
    caption_ = caption; fill_ = fill; border_ = border; ink_ = ink;
    setTitle(caption_.c_str());
    invalid();
  }

  void draw(CDrawContext* context) override {
    context->setFillColor(fill_);
    context->setFrameColor(border_);
    context->setLineWidth(1);
    context->drawRect(getViewSize(), kDrawFilledAndStroked);
    context->setFont(kNormalFontSmall);
    context->setFontColor(ink_);
    context->drawString(caption_.c_str(), getViewSize(), kCenterText, true);
  }

 private:
  bool selectOnly_ = false;
  std::string caption_;
  CColor fill_ = palette::kRaised, border_ = palette::kLine, ink_ = palette::kMuted;
};

std::string format_uint(const char* format, std::uint64_t value) {
  char buffer[32] = {};
  std::snprintf(buffer, sizeof(buffer), format, value);
  return buffer;
}

// Only completed snapshots cross back to the UI thread. At most one pipe
// transaction batch is in flight, including start/stop commands.
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

  void attachErrorView(SharedPointer<CTextLabel> view) {
    errorText_ = std::move(view);
  }

  void attachMatrix(std::vector<SharedPointer<CTextLabel>> inputs,
                    std::vector<SharedPointer<CTextLabel>> outputs,
                    std::vector<SharedPointer<MatrixCellButton>> cells,
                    SharedPointer<CTextLabel> summary,
                    SharedPointer<CTextButton> previousRows,
                    SharedPointer<CTextButton> nextRows,
                    SharedPointer<CTextButton> previousColumns,
                    SharedPointer<CTextButton> nextColumns) {
    inputs_ = std::move(inputs);
    outputs_ = std::move(outputs);
    cells_ = std::move(cells);
    summary_ = std::move(summary);
    navigation_ = {previousRows, nextRows, previousColumns, nextColumns};
    for (auto& cell : cells_) cell->setListener(this);
    for (auto& button : navigation_) button->setListener(this);
    renderMatrix();
  }

  void attachInspector(SharedPointer<CTextLabel> source,
                       SharedPointer<CTextLabel> destination,
                       SharedPointer<CTextLabel> gainText,
                       SharedPointer<CHorizontalSlider> gain,
                       SharedPointer<CTextButton> mute,
                       SharedPointer<CTextButton> reset,
                       SharedPointer<CTextButton> remove) {
    selectedSource_ = std::move(source);
    selectedDestination_ = std::move(destination);
    gainText_ = std::move(gainText);
    gainSlider_ = std::move(gain);
    muteButton_ = std::move(mute);
    resetButton_ = std::move(reset);
    removeButton_ = std::move(remove);
    gainSlider_->setListener(this);
    muteButton_->setListener(this);
    resetButton_->setListener(this);
    removeButton_->setListener(this);
    renderInspector();
  }

  void refresh() {
    if (pending_.valid()) {
      if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      try {
        auto state = pending_.get();
        if (routeRequest_) edits_.complete(state.transportOk && state.lastError.empty());
        routeRequest_ = false;
        engineCommandPending_ = false;
        apply(state);
      } catch (...) {
        edits_.clear();
        routeRequest_ = false;
        engineCommandPending_ = false;
        EngineState state;
        state.lastError = "Engine request failed";
        apply(state);
      }
      if (queuedCommand_ != 0 || edits_.queued()) submit();
      nextPoll_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPollIntervalMs);
      return;
    }
    if (std::chrono::steady_clock::now() >= nextPoll_) submit();
  }

  void valueChanged(CControl* control) override {
    if (control->getTag() == 902 && control->getValue() > 0.5F) {
      showPresetWindow();
      return;
    }
    if (control->getTag() == 903 && control->getValue() > 0.5F) {
      showDiagnosticsWindow();
      return;
    }
    if (control->getTag() == 901 && control->getValue() > 0.5F) {
      showRuntimeDeviceWindow();
      return;
    }
    if (control->getTag() == 900 && control->getValue() > 0.5F) {
      showVirtualDeviceWindow();
      return;
    }
    if (control == gainSlider_.get()) {
      const auto route = selectedRoute();
      if (!route || !gainSlider_->isEditing()) return;
      const float db = -60.0F + gainSlider_->getValueNormalized() * 72.0F;
      const float gain = db <= -60.0F ? 0.0F : std::pow(10.0F, db / 20.0F);
      gainText_->setText(formatGain(gain).c_str());
      queueEdit({RouteAction::Gain, selectedInputId_, selectedOutputId_, gain, false});
      return;
    }
    if (control == muteButton_.get() && control->getValue() > 0.5F) {
      const auto route = selectedRoute();
      if (!route) return;
      queueEdit({RouteAction::Mute, selectedInputId_, selectedOutputId_, 0.0F, !route->muted});
      return;
    }
    if (control == resetButton_.get() && control->getValue() > 0.5F) {
      if (!selectedRoute()) return;
      gainSlider_->setValueNormalized(60.0F / 72.0F);
      gainSlider_->invalid();
      gainText_->setText("0.0 dB");
      queueEdit({RouteAction::Gain, selectedInputId_, selectedOutputId_, 1.0F, false});
      return;
    }
    if (control == removeButton_.get() && control->getValue() > 0.5F) {
      if (!selectedRoute()) return;
      queueEdit({RouteAction::Disconnect, selectedInputId_, selectedOutputId_, 1.0F, false});
      return;
    }
    for (std::size_t i = 0; i < navigation_.size(); ++i) {
      if (control == navigation_[i].get() && control->getValue() > 0.5f) {
        if (!last_.transportOk || !last_.hasMatrix) return;
        if (i == 0) rows_.previous();
        if (i == 1) rows_.next();
        if (i == 2) columns_.previous();
        if (i == 3) columns_.next();
        renderMatrix();
        return;
      }
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
      if (control != cells_[i].get() || control->getValue() <= 0.5f) continue;
      const auto row = rows_.first() + i / kVisibleColumns;
      const auto column = columns_.first() + i % kVisibleColumns;
      if (engineCommandPending_ || queuedCommand_ || !last_.transportOk || !last_.hasMatrix ||
          row >= last_.matrix.inputs.size() || column >= last_.matrix.outputs.size()) return;
      const auto& input = last_.matrix.inputs[row].id;
      const auto& output = last_.matrix.outputs[column].id;
      const bool connected = std::any_of(displayMatrix_.routes.begin(), displayMatrix_.routes.end(),
          [&](const auto& route) { return route.input_id == input && route.output_id == output; });
      selectedInputId_ = input;
      selectedOutputId_ = output;
      error_.clear();
      if (!cells_[i]->selectOnly()) {
        queueEdit({connected ? RouteAction::Disconnect : RouteAction::Connect,
                   input, output, 1.0F, false});
      }
      renderMatrix();
      return;
    }
    if (control != button_.get() || control->getValue() <= 0.5f) {
      return;
    }
    if (!last_.runtimeConfigured) { showRuntimeDeviceWindow(); return; }
    queuedCommand_ = last_.runtimeRunning ? 2 : 1;
    error_.clear();
    setPanelControlEnabled(*button_, false);
    renderMatrix();
    if (!pending_.valid()) submit();
  }

 private:
  void queueEdit(RouteCommand command) {
    if (!last_.transportOk || !last_.hasMatrix || queuedCommand_ || engineCommandPending_) return;
    if (!edits_.enqueue(std::move(command))) {
      error_.observe("Too many pending route edits; wait for the engine", true);
    } else {
      error_.clear();
    }
    displayMatrix_ = edits_.project(last_.matrix);
    renderMatrix();
    if (!pending_.valid()) submit();
  }

  const control::PresetRoute* selectedRoute() const {
    const auto& routes = displayMatrix_.routes;
    const auto found = std::find_if(routes.begin(), routes.end(), [&](const auto& route) {
      return route.input_id == selectedInputId_ && route.output_id == selectedOutputId_;
    });
    return found == routes.end() ? nullptr : &*found;
  }

  static std::string formatGain(float gain) {
    return routeGainText(gain);
  }

  void submit() {
    if (pending_.valid()) return;
    int command = 0;
    if (!edits_.queued() && queuedCommand_ != 0) {
      command = queuedCommand_;
      queuedCommand_ = 0;
    }
    auto route = edits_.begin();
    routeRequest_ = route.has_value();
    engineCommandPending_ = command != 0;
    try {
      std::packaged_task<EngineState()> task([client = client_, command, route] {
        if (route) {
          switch (route->action) {
            case RouteAction::Connect:
              return client->setRoute(route->input, route->output, true);
            case RouteAction::Disconnect:
              return client->setRoute(route->input, route->output, false);
            case RouteAction::Gain:
              return client->setRouteGain(route->input, route->output, route->gain);
            case RouteAction::Mute:
              return client->setRouteMuted(route->input, route->output, route->mute);
          }
        }
        return command == 1 ? client->start()
             : command == 2 ? client->stop() : client->poll();
      });
      auto future = task.get_future();
      std::thread(std::move(task)).detach();
      pending_ = std::move(future);
      renderMatrix();
    } catch (...) {
      edits_.clear();
      routeRequest_ = false;
      engineCommandPending_ = false;
      auto state = last_;
      state.lastError = "Could not start the engine request worker";
      apply(state);
      nextPoll_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPollIntervalMs);
    }
  }

  void apply(EngineState state) {
    error_.observe(state.lastError, state.transportOk);
    state.lastError = error_.message();
    last_ = state;
    if (!state.transportOk || !state.hasMatrix) edits_.clear();
    displayMatrix_ = edits_.project(state.matrix);
    if (dot_) {
      dot_->setColor(!state.transportOk ? palette::kDanger :
                    !state.lastError.empty() ? palette::kWarning :
                    state.runtimeRunning ? palette::kHealthy : palette::kMuted);
    }
    if (statusText_) {
      statusText_->setText(!state.lastError.empty() ? "Action failed"
                          : !state.transportOk ? "Engine offline"
                          : !state.runtimeConfigured ? "Not configured"
                          : state.runtimeRunning ? "Audio running" : "Audio stopped");
    }
    if (errorText_) {
      errorText_->setText(state.lastError.c_str());
      errorText_->setTooltipText(state.lastError.c_str());
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
                        : state.runtimeRunning   ? "Stop audio"
                                                  : "Start audio");
      setPanelControlEnabled(*button_, state.transportOk &&
                             queuedCommand_ == 0 && !engineCommandPending_);
    }
    renderMatrix();
  }

  void renderMatrix() {
    if (cells_.empty()) return;
    const auto& matrix = displayMatrix_;
    const bool available = last_.transportOk && last_.hasMatrix;
    if (available) {
      rows_.reconcile(matrix.inputs.size());
      columns_.reconcile(matrix.outputs.size());
    }
    for (std::size_t row = 0; row < kVisibleRows; ++row) {
      const auto index = rows_.first() + row;
      inputs_[row]->setVisible(available && rows_.contains(row));
      inputs_[row]->setText(available && index < matrix.inputs.size()
          ? matrix.inputs[index].label.c_str() : "");
      inputs_[row]->setTooltipText(available && index < matrix.inputs.size()
          ? matrix.inputs[index].label.c_str() : "");
    }
    for (std::size_t column = 0; column < kVisibleColumns; ++column) {
      const auto index = columns_.first() + column;
      outputs_[column]->setVisible(available && columns_.contains(column));
      outputs_[column]->setText(available && index < matrix.outputs.size()
          ? matrix.outputs[index].label.c_str() : "");
      outputs_[column]->setTooltipText(available && index < matrix.outputs.size()
          ? matrix.outputs[index].label.c_str() : "");
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
      const auto row = rows_.first() + i / kVisibleColumns;
      const auto column = columns_.first() + i % kVisibleColumns;
      const bool valid = available && rows_.contains(i / kVisibleColumns) &&
                         columns_.contains(i % kVisibleColumns);
      const bool connected = valid && std::any_of(matrix.routes.begin(), matrix.routes.end(),
          [&](const auto& route) {
            return route.input_id == matrix.inputs[row].id &&
                   route.output_id == matrix.outputs[column].id;
          });
      const bool selected = valid && matrix.inputs[row].id == selectedInputId_ &&
                            matrix.outputs[column].id == selectedOutputId_;
      const bool muted = connected && std::any_of(matrix.routes.begin(), matrix.routes.end(),
          [&](const auto& route) { return route.input_id == matrix.inputs[row].id &&
              route.output_id == matrix.outputs[column].id && route.muted; });
      cells_[i]->present(valid, connected, muted, selected);
      cells_[i]->setVisible(valid);
      cells_[i]->setMouseEnabled(valid && !engineCommandPending_ && !queuedCommand_);
    }
    summary_->setText(!available ? "Matrix unavailable" :
        (std::to_string(matrix.inputs.size()) + " inputs  /  " +
         std::to_string(matrix.outputs.size()) + " outputs  /  " +
         std::to_string(matrix.routes.size()) + " routes" +
         (edits_.pending() ? "  /  applying" : "")).c_str());
    if (errorText_) errorText_->setText(error_.message().c_str());
    for (std::size_t i = 0; i < navigation_.size(); ++i) {
      const auto& page = i < 2 ? rows_ : columns_;
      navigation_[i]->setVisible(available && (page.hasPrevious() || page.hasNext()));
      setPanelControlEnabled(*navigation_[i], available &&
          (i % 2 == 0 ? page.hasPrevious() : page.hasNext()));
    }
    renderInspector();
  }

  void renderInspector() {
    if (!gainSlider_) return;
    const auto& matrix = displayMatrix_;
    const auto input = std::find_if(matrix.inputs.begin(), matrix.inputs.end(),
        [&](const auto& endpoint) { return endpoint.id == selectedInputId_; });
    const auto output = std::find_if(matrix.outputs.begin(), matrix.outputs.end(),
        [&](const auto& endpoint) { return endpoint.id == selectedOutputId_; });
    const bool selected = last_.transportOk && last_.hasMatrix &&
                          input != matrix.inputs.end() && output != matrix.outputs.end();
    selectedSource_->setText(selected ? input->label.c_str() : "No source selected");
    selectedDestination_->setText(selected ? output->label.c_str() : "No destination selected");
    selectedSource_->setTooltipText(selected ? input->label.c_str() : "");
    selectedDestination_->setTooltipText(selected ? output->label.c_str() : "");
    const auto* route = selected ? selectedRoute() : nullptr;
    const bool active = route != nullptr;
    if (!gainSlider_->isEditing()) {
      const float db = active && route->gain > 0.0F
                           ? std::clamp(20.0F * std::log10(route->gain), -60.0F, 12.0F)
                           : -60.0F;
      const auto normalized = (db + 60.0F) / 72.0F;
      if (gainSlider_->getValueNormalized() != normalized) {
        gainSlider_->setValueNormalized(normalized);
        gainSlider_->invalid();
      }
      gainText_->setText(active ? formatGain(route->gain).c_str() : "-- dB");
    }
    const bool editable = active && !engineCommandPending_ && !queuedCommand_;
    setPanelControlEnabled(*gainSlider_, editable);
    setPanelControlEnabled(*muteButton_, editable);
    muteButton_->setTitle(active && route->muted ? "Unmute" : "Mute");
    setPanelControlEnabled(*resetButton_, editable);
    setPanelControlEnabled(*removeButton_, editable);
  }

  std::shared_ptr<EngineClient> client_ = std::make_shared<EngineClient>();
  std::future<EngineState> pending_;
  std::chrono::steady_clock::time_point nextPoll_{};
  int queuedCommand_ = 0;
  RouteEditQueue edits_;
  control::PresetRouteMatrix displayMatrix_;
  bool routeRequest_ = false;
  bool engineCommandPending_ = false;
  MatrixPage rows_{kVisibleRows};
  MatrixPage columns_{kVisibleColumns};
  std::vector<SharedPointer<CTextLabel>> inputs_;
  std::vector<SharedPointer<CTextLabel>> outputs_;
  std::vector<SharedPointer<MatrixCellButton>> cells_;
  SharedPointer<CTextLabel> summary_;
  std::vector<SharedPointer<CTextButton>> navigation_;
  std::string selectedInputId_;
  std::string selectedOutputId_;
  SharedPointer<CTextLabel> selectedSource_;
  SharedPointer<CTextLabel> selectedDestination_;
  SharedPointer<CTextLabel> gainText_;
  SharedPointer<CHorizontalSlider> gainSlider_;
  SharedPointer<CTextButton> muteButton_;
  SharedPointer<CTextButton> resetButton_;
  SharedPointer<CTextButton> removeButton_;
  GuiRequestErrorState error_;
  SharedPointer<CTextLabel> errorText_;
  EngineState last_;
  SharedPointer<StatusDotView> dot_;
  SharedPointer<CTextLabel> statusText_;
  SharedPointer<LcdReadoutView> hzReadout_;
  SharedPointer<LcdReadoutView> smpReadout_;
  SharedPointer<LcdReadoutView> xrunReadout_;
  SharedPointer<CTextButton> button_;
};

class MainWindowController final : public WindowControllerAdapter {
 public:
  void startPolling(std::shared_ptr<HeaderController> controller) {
    controller_ = std::move(controller);
    timer_ = makeOwned<CVSTGUITimer>([controller = controller_](CVSTGUITimer*) {
      controller->refresh();
    }, 30, true);
    controller_->refresh();
  }

  void onClosed(const IWindow&) override {
    if (timer_) timer_->stop();
    timer_ = nullptr;
    controller_.reset();
  }

 private:
  std::shared_ptr<HeaderController> controller_;
  SharedPointer<CVSTGUITimer> timer_;
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
  config.style.border().close().size();
  config.size = CPoint(kWindowWidth, kWindowHeight);
  config.title = "System Audio Route";

  // Window command/menu dispatch requires a controller, which also owns polling.
  auto windowController = std::make_shared<MainWindowController>();
  auto window = IApplication::instance().createWindow(config, windowController);
  if (!window) {
    return nullptr;
  }

  auto frame = makeOwned<CFrame>(CRect(0, 0, kWindowWidth, kWindowHeight), nullptr);
  frame->setBackgroundColor(palette::kCanvas);
  auto content = makeOwned<CViewContainer>(CRect(0, 0, kWindowWidth, kWindowHeight));
  content->setBackgroundColor(palette::kCanvas);

  auto header = makeOwned<CViewContainer>(CRect(0, 0, kWindowWidth, kHeaderHeight));
  header->setBackgroundColor(palette::kSurface);
  addRetainedView(*content, header);

  CCoord x = 18;
  auto brand = makeLabel(CRect(x, 0, x + 220, kHeaderHeight), "SYSTEM AUDIO ROUTE",
                         palette::kText, true);
  addRetainedView(*header, brand);
  x += 236;

  auto dot = makeOwned<StatusDotView>(CRect(x, kHeaderHeight / 2 - 4, x + 8,
                                            kHeaderHeight / 2 + 4));
  addRetainedView(*header, dot);
  x += 16;

  auto statusText = makeLabel(CRect(x, 0, x + 130, kHeaderHeight), "Engine offline",
                              palette::kText, false);
  addRetainedView(*header, statusText);

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
  stylePanelButton(*startStopButton);
  startStopButton->setRoundRadius(2);
  addRetainedView(*header, startStopButton);

  right -= 10 + readoutWidth;
  auto xrunReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  addRetainedView(*header, xrunReadout);

  right -= 8 + readoutWidth;
  auto smpReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  addRetainedView(*header, smpReadout);

  right -= 8 + readoutWidth;
  auto hzReadout = makeOwned<LcdReadoutView>(
      CRect(right, readoutY, right + readoutWidth, readoutY + readoutHeight));
  addRetainedView(*header, hzReadout);

  auto controller = std::make_shared<HeaderController>();
  auto navigation = makeOwned<CViewContainer>(
      CRect(0, kHeaderHeight, kNavigationWidth, kWindowHeight));
  navigation->setBackgroundColor(palette::kSurface);
  addRetainedView(*content, navigation);
  addRetainedView(*navigation, makeLabel(CRect(16, 12, 148, 38), "WORKSPACE",
                                        palette::kMuted, false));
  auto selectedNavigation = makeOwned<CViewContainer>(CRect(8, 46, 152, 82));
  selectedNavigation->setBackgroundColor(palette::kRaised);
  auto accent = makeOwned<CViewContainer>(CRect(0, 0, 3, 36));
  accent->setBackgroundColor(palette::kSteel);
  addRetainedView(*selectedNavigation, accent);
  addRetainedView(*selectedNavigation, makeLabel(CRect(12, 0, 140, 36),
      "Routing matrix", palette::kText, true));
  addRetainedView(*navigation, selectedNavigation);
  auto makeNavigation = [&](CCoord top, int tag, const char* text, const char* tooltip) {
    auto button = makeOwned<CTextButton>(CRect(8, top, 152, top + 36),
                                        controller.get(), tag, text);
    stylePanelButton(*button);
    button->setTextColor(palette::kText);
    button->setTooltipText(tooltip);
    addRetainedView(*navigation, button);
    return button;
  };
  auto audioDevices = makeNavigation(92, 901, "Audio devices", "Audio devices and VB-CABLE setup");
  makeNavigation(138, 902, "Presets", "Save, load and manage routing presets");
  makeNavigation(184, 903, "Diagnostics", "Engine counters and connection status");
  addRetainedView(*navigation, makeLabel(CRect(16, 270, 148, 296), "EXPERIMENTAL",
                                        palette::kMuted, false));
  makeNavigation(304, 900, "Driver lab", "Experimental virtual driver management");
  addRetainedView(*navigation, makeLabel(CRect(16, 530, 148, 554),
      "Native Preview", palette::kWarning, false));
  addRetainedView(*navigation, makeLabel(CRect(16, 554, 148, 578),
      SAR_VERSION, palette::kMuted, false));

  auto body = makeOwned<CViewContainer>(
      CRect(kNavigationWidth, kHeaderHeight, kWindowWidth, kWindowHeight));
  body->setBackgroundColor(palette::kCanvas);
  auto title = makeLabel(CRect(24, 14, 228, 42), "ROUTING MATRIX", palette::kText, true);
  addRetainedView(*body, title);
  auto summary = makeLabel(CRect(240, 14, 742, 42), "Matrix unavailable", palette::kMuted, false);
  addRetainedView(*body, summary);
  auto errorText = makeOwned<CMultiLineTextLabel>(CRect(24, 44, 742, 80));
  errorText->setBackColor(CColor(0, 0, 0, 0));
  errorText->setFrameColor(CColor(0, 0, 0, 0));
  errorText->setFontColor(palette::kDanger);
  errorText->setFont(kNormalFontSmall);
  errorText->setHoriAlign(kLeftText);
  errorText->setLineLayout(CMultiLineTextLabel::LineLayout::wrap);
  addRetainedView(*body, errorText);
  addRetainedView(*body, makeLabel(CRect(24, 82, 190, 118), "SOURCE / DESTINATION",
                                 palette::kMuted, false));
  constexpr CCoord kGridLeft = 206;
  constexpr CCoord kGridTop = 124;
  constexpr CCoord kCellWidth = 108;
  constexpr CCoord kCellHeight = 46;
  std::vector<SharedPointer<CTextLabel>> inputs;
  std::vector<SharedPointer<CTextLabel>> outputs;
  std::vector<SharedPointer<MatrixCellButton>> cells;
  for (std::size_t column = 0; column < kVisibleColumns; ++column) {
    const auto left = kGridLeft + column * kCellWidth;
    auto label = makeLabel(CRect(left + 5, 82, left + kCellWidth - 5, 118), "",
                           palette::kMuted, false);
    addRetainedView(*body, label);
    outputs.push_back(label);
  }
  for (std::size_t row = 0; row < kVisibleRows; ++row) {
    const auto top = kGridTop + row * kCellHeight;
    auto label = makeLabel(CRect(24, top, kGridLeft - 10, top + kCellHeight), "",
                           palette::kText, false);
    addRetainedView(*body, label);
    inputs.push_back(label);
    for (std::size_t column = 0; column < kVisibleColumns; ++column) {
      const auto left = kGridLeft + column * kCellWidth;
      auto cell = makeOwned<MatrixCellButton>(
          CRect(left + 4, top + 4, left + kCellWidth - 4, top + kCellHeight - 4),
          nullptr, -1, "");
      cell->setGradient(nullptr);
      cell->setFrameColor(palette::kLine);
      cell->setRoundRadius(2);
      cell->setTooltipText("Left click: toggle route. Right click: inspect route.");
      addRetainedView(*body, cell);
      cells.push_back(cell);
    }
  }
  auto makeNav = [&](CCoord left, const char* text) {
    auto button = makeOwned<CTextButton>(CRect(left, 520, left + 82, 550), nullptr, -1, text);
    stylePanelButton(*button);
    button->setFrameColor(palette::kLine);
    button->setTextColor(palette::kText);
    button->setRoundRadius(2);
    addRetainedView(*body, button);
    return button;
  };
  auto previousRows = makeNav(24, "Rows <");
  auto nextRows = makeNav(112, "Rows >");
  auto previousColumns = makeNav(206, "Cols <");
  auto nextColumns = makeNav(294, "Cols >");

  auto inspector = makeOwned<CViewContainer>(CRect(760, 0, 1000, kWindowHeight - kHeaderHeight));
  inspector->setBackgroundColor(palette::kSurface);
  addRetainedView(*body, inspector);
  addRetainedView(*inspector, makeLabel(CRect(16, 14, 220, 42), "ROUTE INSPECTOR",
                               palette::kMuted, true));
  auto selectedSource = makeLabel(CRect(16, 70, 222, 96), "No source selected",
                                  palette::kText, false);
  addRetainedView(*inspector, selectedSource);
  addRetainedView(*inspector, makeLabel(CRect(16, 100, 222, 120), "TO",
                               palette::kMuted, false));
  auto selectedDestination = makeLabel(CRect(16, 124, 222, 150),
                                       "No destination selected", palette::kText, false);
  addRetainedView(*inspector, selectedDestination);
  addRetainedView(*inspector, makeLabel(CRect(16, 196, 222, 218), "GAIN",
                               palette::kMuted, false));
  auto gainText = makeLabel(CRect(16, 226, 222, 252), "-- dB", palette::kText, true);
  addRetainedView(*inspector, gainText);
  auto gainSlider = makeOwned<CHorizontalSlider>(
      CRect(16, 268, 222, 286), nullptr, -1, 16, 222, nullptr, nullptr);
  gainSlider->setDrawStyle(CSlider::kDrawFrame | CSlider::kDrawBack | CSlider::kDrawValue);
  gainSlider->setFrameColor(palette::kLine);
  gainSlider->setBackColor(palette::kCanvas);
  gainSlider->setValueColor(palette::kHealthy);
  gainSlider->setTooltipText("Route gain: -60 to +12 dB");
  addRetainedView(*inspector, gainSlider);
  auto makeInspectorButton = [&](CCoord left, const char* label) {
    auto button = makeOwned<CTextButton>(CRect(left, 312, left + 96, 346),
                                        nullptr, -1, label);
    stylePanelButton(*button);
    button->setFrameColor(palette::kLine);
    button->setTextColor(palette::kText);
    button->setRoundRadius(2);
    addRetainedView(*inspector, button);
    return button;
  };
  auto muteButton = makeInspectorButton(16, "Mute");
  auto resetButton = makeInspectorButton(124, "0 dB");
  auto removeButton = makeOwned<CTextButton>(CRect(16, 374, 220, 408),
                                            nullptr, -1, "Remove route");
  stylePanelButton(*removeButton);
  removeButton->setFrameColor(palette::kLine);
  removeButton->setTextColor(palette::kText);
  removeButton->setRoundRadius(2);
  addRetainedView(*inspector, removeButton);
  addRetainedView(*content, body);

  controller->attachErrorView(errorText);
  controller->attachViews(dot, statusText, hzReadout, smpReadout, xrunReadout,
                          startStopButton);
  controller->attachMatrix(std::move(inputs), std::move(outputs), std::move(cells),
                           summary, previousRows, nextRows, previousColumns, nextColumns);
  controller->attachInspector(selectedSource, selectedDestination, gainText,
                              gainSlider, muteButton, resetButton, removeButton);
  startStopButton->setListener(controller.get());
  auto viewport = makeOwned<ThemedScrollView>(CRect(0, 0, kWindowWidth, kWindowHeight),
      CRect(0, 0, kWindowWidth, kWindowHeight), CScrollView::kHorizontalScrollbar |
      CScrollView::kVerticalScrollbar | CScrollView::kAutoHideScrollbars |
      CScrollView::kDontDrawFrame | CScrollView::kFollowFocusView);
  viewport->setBackgroundColor(palette::kCanvas);
  styleScrollbars(*viewport);
  viewport->setAutosizeFlags(kAutosizeLeft | kAutosizeTop | kAutosizeRight | kAutosizeBottom);
  addRetainedView(*viewport, content);
  addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWindowWidth, kWindowHeight),
      MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST));
  frame->setFocusView(audioDevices);
  viewport->resetScrollOffset();
  windowController->startPolling(std::move(controller));
  return window;
}

}  // namespace sar::gui_vstgui
