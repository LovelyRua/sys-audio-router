#include "app/gui_vstgui/main_window.h"

#include "app/gui_vstgui/engine_client.h"
#include "app/gui_vstgui/audio_setup_model.h"
#include "app/gui_vstgui/lcd_readout_view.h"
#include "app/gui_vstgui/palette.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"

#include <cstdio>
#include <cmath>
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
constexpr std::size_t kVisibleColumns = 5;
constexpr std::size_t kSetupRows = 8;

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

 private:
  bool selectOnly_ = false;
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

  void attachSetup(SharedPointer<CViewContainer> panel,
                   SharedPointer<CTextLabel> status,
                   SharedPointer<CTextButton> addCapture,
                   SharedPointer<CTextButton> addRender,
                   SharedPointer<CTextButton> apply,
                   SharedPointer<CTextButton> cancel,
                   std::vector<SharedPointer<CTextLabel>> directions,
                   std::vector<SharedPointer<CTextLabel>> devices,
                   std::vector<SharedPointer<CTextButton>> previous,
                   std::vector<SharedPointer<CTextButton>> next,
                   std::vector<SharedPointer<CTextButton>> remove) {
    setupPanel_ = std::move(panel);
    setupStatus_ = std::move(status);
    setupAddCapture_ = std::move(addCapture);
    setupAddRender_ = std::move(addRender);
    setupApply_ = std::move(apply);
    setupCancel_ = std::move(cancel);
    setupDirections_ = std::move(directions);
    setupDevices_ = std::move(devices);
    setupPrevious_ = std::move(previous);
    setupNext_ = std::move(next);
    setupRemove_ = std::move(remove);
    setupAddCapture_->setListener(this);
    setupAddRender_->setListener(this);
    setupApply_->setListener(this);
    setupCancel_->setListener(this);
    for (auto& control : setupPrevious_) control->setListener(this);
    for (auto& control : setupNext_) control->setListener(this);
    for (auto& control : setupRemove_) control->setListener(this);
    setupPanel_->setVisible(false);
  }

  void attachSetupButton(SharedPointer<CTextButton> button) {
    setupButton_ = std::move(button);
    setupButton_->setListener(this);
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
      if (queuedCommand_ != 0 || routeCommand_ || runtimeConfigurationCommand_) {
        submit();
      }
      nextPoll_ = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(kPollIntervalMs);
      return;
    }
    if (std::chrono::steady_clock::now() >= nextPoll_) submit();
  }

  void valueChanged(CControl* control) override {
    if (control == setupButton_.get() && control->getValue() > 0.5F) {
      openSetup();
      return;
    }
    if (control == setupCancel_.get() && control->getValue() > 0.5F) {
      closeSetup();
      return;
    }
    if (control == setupAddCapture_.get() && control->getValue() > 0.5F) {
      if (setupModel_.endpoints().size() >= kSetupRows) {
        setSetupStatus("This setup page supports up to eight endpoints.");
      } else if (!setupModel_.add_capture()) {
        setSetupStatus("No unused WASAPI capture device is available.");
      }
      renderSetup();
      return;
    }
    if (control == setupAddRender_.get() && control->getValue() > 0.5F) {
      if (setupModel_.endpoints().size() >= kSetupRows) {
        setSetupStatus("This setup page supports up to eight endpoints.");
      } else if (!setupModel_.add_render()) {
        setSetupStatus("No unused WASAPI render device is available.");
      }
      renderSetup();
      return;
    }
    if (control == setupApply_.get() && control->getValue() > 0.5F) {
      if (setupUnsupported_) {
        setSetupStatus("Physical ASIO configuration is not available in this panel.");
        return;
      }
      if (!setupModel_.can_apply()) {
        setSetupStatus("Add a valid render endpoint before applying the matrix.");
        return;
      }
      runtimeConfigurationCommand_ = setupModel_.configuration();
      setupApplyPending_ = true;
      setSetupStatus("Applying topology. The engine may restart its audio runtime.");
      renderSetup();
      if (!pending_.valid()) submit();
      return;
    }
    for (std::size_t i = 0; i < kSetupRows; ++i) {
      if (i >= setupModel_.endpoints().size()) continue;
      if (control == setupPrevious_[i].get() && control->getValue() > 0.5F) {
        if (!setupModel_.cycle_device(i, false)) {
          setSetupStatus("No other compatible unused device is available.");
        }
        renderSetup();
        return;
      }
      if (control == setupNext_[i].get() && control->getValue() > 0.5F) {
        if (!setupModel_.cycle_device(i, true)) {
          setSetupStatus("No other compatible unused device is available.");
        }
        renderSetup();
        return;
      }
      if (control == setupRemove_[i].get() && control->getValue() > 0.5F) {
        static_cast<void>(setupModel_.remove(i));
        renderSetup();
        return;
      }
    }
    if (control == gainSlider_.get()) {
      const auto route = selectedRoute();
      if (!route || !gainSlider_->isEditing()) return;
      const float db = -60.0F + gainSlider_->getValueNormalized() * 72.0F;
      const float gain = db <= -60.0F ? 0.0F : std::pow(10.0F, db / 20.0F);
      gainText_->setText(formatGain(gain).c_str());
      routeCommand_ = RouteCommand{RouteAction::Gain, selectedInputId_,
                                   selectedOutputId_, gain, false};
      if (!pending_.valid()) submit();
      return;
    }
    if (control == muteButton_.get() && control->getValue() > 0.5F) {
      const auto route = selectedRoute();
      if (!route) return;
      routeCommand_ = RouteCommand{RouteAction::Mute, selectedInputId_,
                                   selectedOutputId_, 0.0F, !route->muted};
      if (!pending_.valid()) submit();
      return;
    }
    if (control == resetButton_.get() && control->getValue() > 0.5F) {
      if (!selectedRoute()) return;
      gainSlider_->setValueNormalized(60.0F / 72.0F);
      gainText_->setText("0.0 dB");
      routeCommand_ = RouteCommand{RouteAction::Gain, selectedInputId_,
                                   selectedOutputId_, 1.0F, false};
      if (!pending_.valid()) submit();
      return;
    }
    if (control == removeButton_.get() && control->getValue() > 0.5F) {
      if (!selectedRoute() || pending_.valid()) return;
      routeCommand_ = RouteCommand{RouteAction::Disconnect, selectedInputId_,
                                   selectedOutputId_, 1.0F, false};
      submit();
      return;
    }
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
      selectedInputId_ = input;
      selectedOutputId_ = output;
      if (!cells_[i]->selectOnly()) {
        routeCommand_ = RouteCommand{connected ? RouteAction::Disconnect : RouteAction::Connect,
                                     input, output, 1.0F, false};
      }
      error_.clear();
      renderMatrix();
      if (routeCommand_) submit();
      return;
    }
    if (control != button_.get() || control->getValue() <= 0.5f) {
      return;
    }
    if (!last_.runtimeConfigured) {
      openSetup();
      return;
    }
    queuedCommand_ = last_.runtimeRunning ? 2 : 1;
    error_.clear();
    button_->setMouseEnabled(false);
    if (!pending_.valid()) submit();
  }

 private:
  enum class RouteAction { Connect, Disconnect, Gain, Mute };
  struct RouteCommand {
    RouteAction action;
    std::string input;
    std::string output;
    float gain;
    bool mute;
  };

  const control::PresetRoute* selectedRoute() const {
    const auto& routes = last_.matrix.routes;
    const auto found = std::find_if(routes.begin(), routes.end(), [&](const auto& route) {
      return route.input_id == selectedInputId_ && route.output_id == selectedOutputId_;
    });
    return found == routes.end() ? nullptr : &*found;
  }

  static std::string formatGain(float gain) {
    if (gain <= 0.001F) return "-inf dB";
    char buffer[32] = {};
    std::snprintf(buffer, sizeof(buffer), "%.1f dB", 20.0 * std::log10(gain));
    return buffer;
  }

  void submit() {
    int command = 0;
    if (!routeCommand_ && queuedCommand_ != 0) {
      command = queuedCommand_;
      queuedCommand_ = 0;
    }
    auto route = std::move(routeCommand_);
    routeCommand_.reset();
    auto runtime_configuration = std::move(runtimeConfigurationCommand_);
    runtimeConfigurationCommand_.reset();
    try {
      pending_ = std::async(std::launch::async,
                            [client = client_, command, route,
                             runtime_configuration = std::move(runtime_configuration)] {
        if (runtime_configuration) {
          return client->configureAudioRuntime(*runtime_configuration);
        }
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
    if (setupApplyPending_ && !runtimeConfigurationCommand_) {
      setupApplyPending_ = false;
      if (state.transportOk && state.runtimeConfigured && state.lastError.empty()) {
        closeSetup();
        setupModel_.set_devices(state.devices);
        setupModel_.load(state.audioRuntimeConfiguration);
      } else {
        setSetupStatus(state.lastError.empty()
                           ? "The engine did not confirm the new audio topology."
                           : state.lastError);
        renderSetup();
      }
    }
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
      button_->setMouseEnabled(state.transportOk && queuedCommand_ == 0 &&
                               !setupApplyPending_);
    }
    if (setupButton_) {
      setupButton_->setMouseEnabled(state.transportOk && !setupApplyPending_);
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
      const bool selected = valid && matrix.inputs[row].id == selectedInputId_ &&
                            matrix.outputs[column].id == selectedOutputId_;
      cells_[i]->setTitle(valid ? (connected ? "ON" : "+") : "");
      cells_[i]->setTextColor(connected ? palette::kHealthy : palette::kMuted);
      cells_[i]->setFrameColor(selected ? palette::kHealthy : palette::kLine);
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
    renderInspector();
  }

  void renderInspector() {
    if (!gainSlider_) return;
    const auto& matrix = last_.matrix;
    const auto input = std::find_if(matrix.inputs.begin(), matrix.inputs.end(),
        [&](const auto& endpoint) { return endpoint.id == selectedInputId_; });
    const auto output = std::find_if(matrix.outputs.begin(), matrix.outputs.end(),
        [&](const auto& endpoint) { return endpoint.id == selectedOutputId_; });
    const bool selected = last_.transportOk && last_.hasMatrix &&
                          input != matrix.inputs.end() && output != matrix.outputs.end();
    selectedSource_->setText(selected ? input->label.c_str() : "No source selected");
    selectedDestination_->setText(selected ? output->label.c_str() : "No destination selected");
    const auto* route = selected ? selectedRoute() : nullptr;
    const bool active = route != nullptr;
    if (!gainSlider_->isEditing() && !routeCommand_) {
      const float db = active && route->gain > 0.001F
                           ? std::clamp(20.0F * std::log10(route->gain), -60.0F, 12.0F)
                           : -60.0F;
      gainSlider_->setValueNormalized((db + 60.0F) / 72.0F);
      gainText_->setText(active ? formatGain(route->gain).c_str() : "-- dB");
    }
    gainSlider_->setMouseEnabled(active);
    muteButton_->setMouseEnabled(active && !pending_.valid());
    muteButton_->setTitle(active && route->muted ? "Unmute" : "Mute");
    resetButton_->setMouseEnabled(active && !pending_.valid());
    removeButton_->setMouseEnabled(active && !pending_.valid());
  }

  void setSetupStatus(const std::string& message) {
    if (setupStatus_) setupStatus_->setText(message.c_str());
  }

  void openSetup() {
    if (!setupPanel_) return;
    error_.clear();
    if (errorText_) errorText_->setText("");
    setupModel_.set_devices(last_.devices);
    setupModel_.load(last_.audioRuntimeConfiguration);
    setupUnsupported_ =
        last_.audioRuntimeConfiguration.mode ==
            control::AudioRuntimeMode::PhysicalAsio ||
        std::ranges::any_of(last_.audioRuntimeConfiguration.endpoints,
                            [](const auto& endpoint) {
                              return endpoint.backend ==
                                     control::AudioRuntimeEndpointBackend::PhysicalAsio;
                            });
    setupPanel_->setVisible(true);
    if (!last_.transportOk) {
      setSetupStatus("Connect to the audio engine to configure devices.");
    } else if (setupUnsupported_) {
      setSetupStatus("Physical ASIO is in the active topology. This page leaves it unchanged.");
    } else if (last_.devices.empty()) {
      setSetupStatus("The engine returned no audio devices. Refresh after checking the engine.");
    } else {
      setSetupStatus("Select one or more WASAPI endpoints. At least one render endpoint is required.");
    }
    renderSetup();
  }

  void closeSetup() {
    if (setupPanel_) setupPanel_->setVisible(false);
    setupApplyPending_ = false;
  }

  void renderSetup() {
    if (!setupPanel_) return;
    const auto& endpoints = setupModel_.endpoints();
    for (std::size_t i = 0; i < kSetupRows; ++i) {
      const bool active = i < endpoints.size();
      if (!active) {
        setupDirections_[i]->setText("");
        setupDevices_[i]->setText("");
        setupPrevious_[i]->setMouseEnabled(false);
        setupNext_[i]->setMouseEnabled(false);
        setupRemove_[i]->setMouseEnabled(false);
        continue;
      }
      const auto& endpoint = endpoints[i];
      const bool capture = endpoint.direction ==
                           control::AudioRuntimeEndpointDirection::Capture;
      const auto* device = setupModel_.device_for(endpoint);
      setupDirections_[i]->setText(capture ? "CAPTURE" : "RENDER");
      const auto text = device == nullptr
                            ? "Unavailable device"
                            : device->label + "  |  " +
                                  std::to_string(endpoint.channel_count) + " ch";
      setupDevices_[i]->setText(text.c_str());
      setupPrevious_[i]->setMouseEnabled(!setupUnsupported_ && !setupApplyPending_);
      setupNext_[i]->setMouseEnabled(!setupUnsupported_ && !setupApplyPending_);
      setupRemove_[i]->setMouseEnabled(!setupUnsupported_ && !setupApplyPending_);
    }
    const bool editable = !setupUnsupported_ && !setupApplyPending_;
    const bool below_visible_limit = endpoints.size() < kSetupRows;
    setupAddCapture_->setMouseEnabled(editable && below_visible_limit &&
                                      setupModel_.can_add_capture());
    setupAddRender_->setMouseEnabled(editable && below_visible_limit &&
                                     setupModel_.can_add_render());
    setupApply_->setMouseEnabled(editable && setupModel_.can_apply());
    setupCancel_->setMouseEnabled(!setupApplyPending_);
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
  std::string error_;
  SharedPointer<CTextLabel> errorText_;
  EngineState last_;
  SharedPointer<StatusDotView> dot_;
  SharedPointer<CTextLabel> statusText_;
  SharedPointer<LcdReadoutView> hzReadout_;
  SharedPointer<LcdReadoutView> smpReadout_;
  SharedPointer<LcdReadoutView> xrunReadout_;
  SharedPointer<CTextButton> button_;
  SharedPointer<CTextButton> setupButton_;
  SharedPointer<CViewContainer> setupPanel_;
  SharedPointer<CTextLabel> setupStatus_;
  SharedPointer<CTextButton> setupAddCapture_;
  SharedPointer<CTextButton> setupAddRender_;
  SharedPointer<CTextButton> setupApply_;
  SharedPointer<CTextButton> setupCancel_;
  std::vector<SharedPointer<CTextLabel>> setupDirections_;
  std::vector<SharedPointer<CTextLabel>> setupDevices_;
  std::vector<SharedPointer<CTextButton>> setupPrevious_;
  std::vector<SharedPointer<CTextButton>> setupNext_;
  std::vector<SharedPointer<CTextButton>> setupRemove_;
  AudioSetupModel setupModel_;
  std::optional<control::AudioRuntimeConfiguration> runtimeConfigurationCommand_;
  bool setupApplyPending_ = false;
  bool setupUnsupported_ = false;
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
  auto summary = makeLabel(CRect(300, 14, 620, 42), "Matrix unavailable", palette::kMuted, false);
  body->addView(summary);
  auto setupButton = makeOwned<CTextButton>(CRect(630, 12, 742, 42), nullptr, -1,
                                             "Audio setup");
  setupButton->setGradient(nullptr);
  setupButton->setFrameColor(palette::kLine);
  setupButton->setTextColor(palette::kText);
  setupButton->setRoundRadius(2);
  body->addView(setupButton);
  auto errorText = makeLabel(CRect(24, 44, kWindowWidth - 24, 70), "",
                            palette::kDanger, false);
  body->addView(errorText);
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
      auto cell = makeOwned<MatrixCellButton>(
          CRect(left + 4, top + 4, left + kCellWidth - 4, top + kCellHeight - 4),
          nullptr, -1, "");
      cell->setGradient(nullptr);
      cell->setFrameColor(palette::kLine);
      cell->setRoundRadius(2);
      cell->setTooltipText("Left click: toggle route. Right click: inspect route.");
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
  auto previousColumns = makeNav(206, "Cols <");
  auto nextColumns = makeNav(294, "Cols >");

  auto inspector = makeOwned<CViewContainer>(CRect(760, 0, 1000, kWindowHeight - kHeaderHeight));
  inspector->setBackgroundColor(palette::kSurface);
  body->addView(inspector);
  inspector->addView(makeLabel(CRect(16, 14, 220, 42), "ROUTE INSPECTOR",
                               palette::kMuted, true));
  auto selectedSource = makeLabel(CRect(16, 70, 222, 96), "No source selected",
                                  palette::kText, false);
  inspector->addView(selectedSource);
  inspector->addView(makeLabel(CRect(16, 100, 222, 120), "TO",
                               palette::kMuted, false));
  auto selectedDestination = makeLabel(CRect(16, 124, 222, 150),
                                       "No destination selected", palette::kText, false);
  inspector->addView(selectedDestination);
  inspector->addView(makeLabel(CRect(16, 196, 222, 218), "GAIN",
                               palette::kMuted, false));
  auto gainText = makeLabel(CRect(16, 226, 222, 252), "-- dB", palette::kText, true);
  inspector->addView(gainText);
  auto gainSlider = makeOwned<CHorizontalSlider>(
      CRect(16, 268, 222, 286), nullptr, -1, 16, 222, nullptr, nullptr);
  gainSlider->setDrawStyle(CSlider::kDrawFrame | CSlider::kDrawBack | CSlider::kDrawValue);
  gainSlider->setFrameColor(palette::kLine);
  gainSlider->setBackColor(palette::kCanvas);
  gainSlider->setValueColor(palette::kHealthy);
  gainSlider->setTooltipText("Route gain: -60 to +12 dB");
  inspector->addView(gainSlider);
  auto makeInspectorButton = [&](CCoord left, const char* label) {
    auto button = makeOwned<CTextButton>(CRect(left, 312, left + 96, 346),
                                        nullptr, -1, label);
    button->setGradient(nullptr);
    button->setFrameColor(palette::kLine);
    button->setTextColor(palette::kText);
    button->setRoundRadius(2);
    inspector->addView(button);
    return button;
  };
  auto muteButton = makeInspectorButton(16, "Mute");
  auto resetButton = makeInspectorButton(124, "0 dB");
  auto removeButton = makeOwned<CTextButton>(CRect(16, 374, 220, 408),
                                            nullptr, -1, "Remove route");
  removeButton->setGradient(nullptr);
  removeButton->setFrameColor(palette::kLine);
  removeButton->setTextColor(palette::kText);
  removeButton->setRoundRadius(2);
  inspector->addView(removeButton);

  auto setupPanel = makeOwned<CViewContainer>(
      CRect(0, 0, kWindowWidth, kWindowHeight - kHeaderHeight));
  setupPanel->setBackgroundColor(palette::kCanvas);
  setupPanel->addView(makeLabel(CRect(24, 18, 580, 48), "WASAPI MATRIX SETUP",
                                palette::kText, true));
  setupPanel->addView(makeLabel(CRect(24, 56, 900, 82),
                                "Choose native devices for graph capture and render endpoints.",
                                palette::kMuted, false));
  setupPanel->addView(makeLabel(CRect(24, 112, 160, 136), "DIRECTION",
                                palette::kMuted, true));
  setupPanel->addView(makeLabel(CRect(176, 112, 480, 136), "DEVICE / CHANNELS",
                                palette::kMuted, true));
  std::vector<SharedPointer<CTextLabel>> setupDirections;
  std::vector<SharedPointer<CTextLabel>> setupDevices;
  std::vector<SharedPointer<CTextButton>> setupPrevious;
  std::vector<SharedPointer<CTextButton>> setupNext;
  std::vector<SharedPointer<CTextButton>> setupRemove;
  for (std::size_t i = 0; i < kSetupRows; ++i) {
    const auto top = 140 + static_cast<CCoord>(i) * 44;
    auto row = makeOwned<CViewContainer>(CRect(20, top, 980, top + 38));
    row->setBackgroundColor(palette::kSurface);
    setupPanel->addView(row);
    auto direction = makeLabel(CRect(28, top, 164, top + 38), "",
                               palette::kSteel, true);
    setupPanel->addView(direction);
    setupDirections.push_back(direction);
    auto device = makeLabel(CRect(178, top, 488, top + 38), "",
                            palette::kText, false);
    setupPanel->addView(device);
    setupDevices.push_back(device);
    auto makeSetupButton = [&](CCoord left, CCoord width, const char* text) {
      auto button = makeOwned<CTextButton>(
          CRect(left, top + 4, left + width, top + 34), nullptr, -1, text);
      button->setGradient(nullptr);
      button->setFrameColor(palette::kLine);
      button->setTextColor(palette::kText);
      button->setRoundRadius(2);
      setupPanel->addView(button);
      return button;
    };
    setupPrevious.push_back(makeSetupButton(500, 42, "<"));
    setupNext.push_back(makeSetupButton(548, 42, ">"));
    setupRemove.push_back(makeSetupButton(604, 86, "Remove"));
  }
  auto setupAddCapture = makeOwned<CTextButton>(CRect(24, 504, 166, 538),
                                                 nullptr, -1, "+ Capture");
  auto setupAddRender = makeOwned<CTextButton>(CRect(174, 504, 316, 538),
                                                nullptr, -1, "+ Render");
  auto setupStatus = makeLabel(CRect(24, 548, 690, 580), "",
                               palette::kMuted, false);
  auto setupApply = makeOwned<CTextButton>(CRect(786, 530, 882, 566),
                                            nullptr, -1, "Apply");
  auto setupCancel = makeOwned<CTextButton>(CRect(890, 530, 976, 566),
                                             nullptr, -1, "Cancel");
  for (const auto& button : {setupAddCapture, setupAddRender, setupApply, setupCancel}) {
    button->setGradient(nullptr);
    button->setFrameColor(palette::kLine);
    button->setTextColor(palette::kText);
    button->setRoundRadius(2);
    setupPanel->addView(button);
  }
  setupPanel->addView(setupStatus);
  body->addView(setupPanel);
  frame->addView(body);

  auto controller = std::make_shared<HeaderController>();
  controller->attachErrorView(errorText);
  controller->attachViews(dot, statusText, hzReadout, smpReadout, xrunReadout,
                          startStopButton);
  controller->attachMatrix(std::move(inputs), std::move(outputs), std::move(cells),
                           summary, previousRows, nextRows, previousColumns, nextColumns);
  controller->attachInspector(selectedSource, selectedDestination, gainText,
                              gainSlider, muteButton, resetButton, removeButton);
  controller->attachSetup(setupPanel, setupStatus, setupAddCapture, setupAddRender,
                          setupApply, setupCancel, std::move(setupDirections),
                          std::move(setupDevices), std::move(setupPrevious),
                          std::move(setupNext), std::move(setupRemove));
  controller->attachSetupButton(setupButton);
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
