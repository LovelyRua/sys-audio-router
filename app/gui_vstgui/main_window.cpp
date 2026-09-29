#include "app/gui_vstgui/main_window.h"

#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/lcd_readout_view.h"
#include "app/gui_vstgui/palette.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"

#include <cstdio>
#include <chrono>
#include <future>
#include <memory>
#include <vector>
#include <algorithm>
#include <optional>

using namespace VSTGUI;
using namespace VSTGUI::Standalone;

namespace sar::gui_vstgui {
namespace {

constexpr CCoord kWindowWidth = 1000;
constexpr CCoord kWindowHeight = 640;
constexpr CCoord kHeaderHeight = 54;
constexpr std::uint32_t kPollIntervalMs = 750;
constexpr std::size_t kVisibleRows = 8;
constexpr std::size_t kVisibleColumns = 7;

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
                    std::vector<SharedPointer<CTextButton>> cells,
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

  void refresh() {
    if (pending_.valid()) {
      if (pending_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
      try {
        auto state = pending_.get();
        if (!state.lastError.empty()) error_ = state.lastError;
        state.lastError = error_;
        apply(state);
      } catch (...) {
        EngineState state;
        state.lastError = "Engine request failed";
        apply(state);
      }
      if (queuedCommand_ != 0 || routeCommand_) submit();
      nextPoll_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPollIntervalMs);
      return;
    }
    if (std::chrono::steady_clock::now() >= nextPoll_) submit();
  }

  void valueChanged(CControl* control) override {
    for (std::size_t i = 0; i < navigation_.size(); ++i) {
      if (control == navigation_[i].get() && control->getValue() > 0.5f) {
        if (i == 0 && rowOffset_ > 0) --rowOffset_;
        if (i == 1 && (rowOffset_ + 1) * kVisibleRows < last_.matrix.inputs.size()) ++rowOffset_;
        if (i == 2 && columnOffset_ > 0) --columnOffset_;
        if (i == 3 && (columnOffset_ + 1) * kVisibleColumns < last_.matrix.outputs.size()) ++columnOffset_;
        renderMatrix();
        return;
      }
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
      if (control != cells_[i].get() || control->getValue() <= 0.5f) continue;
      const auto row = rowOffset_ * kVisibleRows + i / kVisibleColumns;
      const auto column = columnOffset_ * kVisibleColumns + i % kVisibleColumns;
      if (pending_.valid() || !last_.transportOk || !last_.hasMatrix ||
          row >= last_.matrix.inputs.size() || column >= last_.matrix.outputs.size()) return;
      const auto& input = last_.matrix.inputs[row].id;
      const auto& output = last_.matrix.outputs[column].id;
      const bool connected = std::any_of(last_.matrix.routes.begin(), last_.matrix.routes.end(),
          [&](const auto& route) { return route.input_id == input && route.output_id == output; });
      routeCommand_ = RouteCommand{input, output, !connected};
      error_.clear();
      renderMatrix();
      submit();
      return;
    }
    if (control != button_.get() || control->getValue() <= 0.5f) {
      return;
    }
    queuedCommand_ = last_.runtimeRunning ? 2 : 1;
    error_.clear();
    button_->setMouseEnabled(false);
    if (!pending_.valid()) submit();
  }

 private:
  struct RouteCommand { std::string input; std::string output; bool connect; };

  void submit() {
    int command = 0;
    if (queuedCommand_ != 0) {
      command = queuedCommand_;
      queuedCommand_ = 0;
    }
    auto route = std::move(routeCommand_);
    routeCommand_.reset();
    try {
      pending_ = std::async(std::launch::async, [client = client_, command, route] {
        if (route) return client->setRoute(route->input, route->output, route->connect);
        return command == 1 ? client->start()
             : command == 2 ? client->stop() : client->poll();
      });
    } catch (...) {
      auto state = last_;
      state.lastError = "Could not start the engine request worker";
      apply(state);
      nextPoll_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPollIntervalMs);
    }
  }

  void apply(const EngineState& state) {
    last_ = state;
    if (dot_) {
      dot_->setColor(state.transportOk ? palette::kHealthy : palette::kDanger);
    }
    if (statusText_) {
      statusText_->setText(!state.lastError.empty() ? "Action failed"
                          : state.transportOk ? "Engine online" : "Engine offline");
    }
    if (errorText_) {
      errorText_->setText(state.lastError.c_str());
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
      button_->setMouseEnabled(state.transportOk && state.runtimeConfigured &&
                               queuedCommand_ == 0);
    }
    renderMatrix();
  }

  void renderMatrix() {
    if (cells_.empty()) return;
    const auto& matrix = last_.matrix;
    const bool available = last_.transportOk && last_.hasMatrix;
    for (std::size_t row = 0; row < kVisibleRows; ++row) {
      const auto index = rowOffset_ * kVisibleRows + row;
      inputs_[row]->setText(available && index < matrix.inputs.size()
          ? matrix.inputs[index].label.c_str() : "");
    }
    for (std::size_t column = 0; column < kVisibleColumns; ++column) {
      const auto index = columnOffset_ * kVisibleColumns + column;
      outputs_[column]->setText(available && index < matrix.outputs.size()
          ? matrix.outputs[index].label.c_str() : "");
    }
    for (std::size_t i = 0; i < cells_.size(); ++i) {
      const auto row = rowOffset_ * kVisibleRows + i / kVisibleColumns;
      const auto column = columnOffset_ * kVisibleColumns + i % kVisibleColumns;
      const bool valid = available && row < matrix.inputs.size() && column < matrix.outputs.size();
      const bool connected = valid && std::any_of(matrix.routes.begin(), matrix.routes.end(),
          [&](const auto& route) {
            return route.input_id == matrix.inputs[row].id &&
                   route.output_id == matrix.outputs[column].id;
          });
      cells_[i]->setTitle(valid ? (connected ? "ON" : "+") : "");
      cells_[i]->setTextColor(connected ? palette::kHealthy : palette::kMuted);
      cells_[i]->setMouseEnabled(valid && !pending_.valid() && !routeCommand_);
    }
    summary_->setText(!available ? "Matrix unavailable" :
        (std::to_string(matrix.inputs.size()) + " inputs  /  " +
         std::to_string(matrix.outputs.size()) + " outputs  /  " +
         std::to_string(matrix.routes.size()) + " routes").c_str());
    navigation_[0]->setMouseEnabled(rowOffset_ > 0);
    navigation_[1]->setMouseEnabled(available && (rowOffset_ + 1) * kVisibleRows < matrix.inputs.size());
    navigation_[2]->setMouseEnabled(columnOffset_ > 0);
    navigation_[3]->setMouseEnabled(available && (columnOffset_ + 1) * kVisibleColumns < matrix.outputs.size());
  }

  std::shared_ptr<EngineClient> client_ = std::make_shared<EngineClient>();
  std::future<EngineState> pending_;
  std::chrono::steady_clock::time_point nextPoll_{};
  int queuedCommand_ = 0;
  std::optional<RouteCommand> routeCommand_;
  std::size_t rowOffset_ = 0;
  std::size_t columnOffset_ = 0;
  std::vector<SharedPointer<CTextLabel>> inputs_;
  std::vector<SharedPointer<CTextLabel>> outputs_;
  std::vector<SharedPointer<CTextButton>> cells_;
  SharedPointer<CTextLabel> summary_;
  std::vector<SharedPointer<CTextButton>> navigation_;
  std::string error_;
  SharedPointer<CTextLabel> errorText_;
  EngineState last_;
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
  config.style.border().close().centered();
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
  auto title = makeLabel(CRect(24, 14, 300, 42), "ROUTING MATRIX", palette::kText, true);
  body->addView(title);
  auto summary = makeLabel(CRect(300, 14, 780, 42), "Matrix unavailable", palette::kMuted, false);
  body->addView(summary);
  auto errorText = makeLabel(CRect(24, 44, kWindowWidth - 24, 70), "",
                            palette::kDanger, false);
  body->addView(errorText);
  constexpr CCoord kGridLeft = 206;
  constexpr CCoord kGridTop = 124;
  constexpr CCoord kCellWidth = 108;
  constexpr CCoord kCellHeight = 46;
  std::vector<SharedPointer<CTextLabel>> inputs;
  std::vector<SharedPointer<CTextLabel>> outputs;
  std::vector<SharedPointer<CTextButton>> cells;
  for (std::size_t column = 0; column < kVisibleColumns; ++column) {
    const auto left = kGridLeft + column * kCellWidth;
    auto label = makeLabel(CRect(left + 5, 82, left + kCellWidth - 5, 118), "",
                           palette::kMuted, false);
    body->addView(label);
    outputs.push_back(label);
  }
  for (std::size_t row = 0; row < kVisibleRows; ++row) {
    const auto top = kGridTop + row * kCellHeight;
    auto label = makeLabel(CRect(24, top, kGridLeft - 10, top + kCellHeight), "",
                           palette::kText, false);
    body->addView(label);
    inputs.push_back(label);
    for (std::size_t column = 0; column < kVisibleColumns; ++column) {
      const auto left = kGridLeft + column * kCellWidth;
      auto cell = makeOwned<CTextButton>(
          CRect(left + 4, top + 4, left + kCellWidth - 4, top + kCellHeight - 4),
          nullptr, -1, "");
      cell->setGradient(nullptr);
      cell->setFrameColor(palette::kLine);
      cell->setRoundRadius(2);
      body->addView(cell);
      cells.push_back(cell);
    }
  }
  auto makeNav = [&](CCoord left, const char* text) {
    auto button = makeOwned<CTextButton>(CRect(left, 520, left + 82, 550), nullptr, -1, text);
    button->setGradient(nullptr);
    button->setFrameColor(palette::kLine);
    button->setTextColor(palette::kText);
    button->setRoundRadius(2);
    body->addView(button);
    return button;
  };
  auto previousRows = makeNav(24, "Rows <");
  auto nextRows = makeNav(112, "Rows >");
  auto previousColumns = makeNav(776, "Cols <");
  auto nextColumns = makeNav(864, "Cols >");
  frame->addView(body);

  auto controller = std::make_shared<HeaderController>();
  controller->attachErrorView(errorText);
  controller->attachViews(dot, statusText, hzReadout, smpReadout, xrunReadout,
                          startStopButton);
  controller->attachMatrix(std::move(inputs), std::move(outputs), std::move(cells),
                           summary, previousRows, nextRows, previousColumns, nextColumns);
  startStopButton->setListener(controller.get());
  // The controller and its views must outlive the timer, which is the case
  // here: the frame (owned by the window) holds the views, and this lambda
  // holds the last strong reference to the controller for the process's
  // lifetime, matching CVSTGUITimer's own fire-and-forget ownership model.
  new CVSTGUITimer([controller](CVSTGUITimer*) { controller->refresh(); },
                   30, true);
  controller->refresh();

  window->setContentView(frame);
  return window;
}

}  // namespace sar::gui_vstgui
