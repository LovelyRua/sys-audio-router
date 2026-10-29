#include "app/gui_vstgui/diagnostics_window.h"
#include "app/gui_vstgui/command_ids.h"
#include "app/gui_vstgui/palette.h"
#include "app/gui_vstgui/panel_button.h"
#include "app/gui_vstgui/view_ownership.h"
#include "app/gui_vstgui/window_geometry.h"
#include "core/control/control_wire_protocol.h"
#include "core/platform/windows_current_user_sid.h"
#include "core/service/windows_named_pipe_control.h"

#include "vstgui/lib/cfont.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/standalone/include/iapplication.h"
#include "vstgui/standalone/include/helpers/windowcontroller.h"
#include "vstgui/standalone/include/helpers/windowlistener.h"

#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace sar::gui_vstgui {
namespace {
using namespace VSTGUI;
using namespace VSTGUI::Standalone;
using Counters = diagnostics::EngineDiagnostics;
using Recovery = control::WasapiRecoveryDiagnostics;
using Clock = std::chrono::steady_clock;
constexpr const char* kTitle = "Diagnostics";
constexpr CCoord kWidth = 860, kHeight = 940, kViewportHeight = 650;

struct Snapshot {
  bool offline = false;
  bool valid = false;
  std::string error;
  control::ControlResponse response;
};

Snapshot readDiagnostics() {
  Snapshot result;
  GuiCommandIds ids;
  control::ControlCommand command;
  command.command_id = ids.next();
  command.type = control::ControlCommandType::QueryDiagnostics;
  const auto encoded = control::encode_control_command(command);
  if (!encoded.ok()) {
    result.error = "Could not encode diagnostics request";
    return result;
  }
  std::vector<std::byte> bytes;
  bytes.reserve(encoded.bytes.size());
  for (auto byte : encoded.bytes) bytes.push_back(static_cast<std::byte>(byte));
  service::NamedPipeControlConfig config;
  config.pipe_name = platform::default_control_pipe_name();
  const auto transaction = service::transact_named_pipe_control(config, bytes, 2000);
  if (!transaction.ok()) {
    result.offline = true;
    result.error = transaction.error().message;
    return result;
  }
  std::vector<std::uint8_t> payload;
  payload.reserve(transaction.payload().size());
  for (auto byte : transaction.payload()) payload.push_back(std::to_integer<std::uint8_t>(byte));
  auto decoded = control::decode_control_response(payload);
  if (!decoded.ok() || decoded.response.command_id != command.command_id) {
    result.error = "Invalid or mismatched diagnostics response";
    return result;
  }
  if (decoded.response.status != control::ControlResponseStatus::Accepted) {
    result.error = decoded.response.errors.empty() ? "Diagnostics request rejected"
                                                 : decoded.response.errors.front().message;
    return result;
  }
  if (!decoded.response.has_diagnostics) {
    result.error = "Engine replied without diagnostics; counters are unknown";
    return result;
  }
  result.valid = true;
  result.response = std::move(decoded.response);
  return result;
}

struct CounterField { const char* label; std::uint64_t Counters::*member; };
constexpr CounterField kCounters[] = {
    {"Graph version", &Counters::graph_version},
    {"Processed blocks", &Counters::processed_blocks},
    {"Xruns", &Counters::xrun_count},
    {"Capture FIFO fill (frames)", &Counters::capture_fifo_fill_frames},
    {"Render FIFO fill (frames)", &Counters::render_fifo_fill_frames},
    {"Capture overflow (cycles)", &Counters::capture_fifo_overflow_cycles},
    {"Capture overflow (frames)", &Counters::capture_fifo_overflow_frames},
    {"Render overflow (cycles)", &Counters::render_fifo_overflow_cycles},
    {"Render overflow (frames)", &Counters::render_fifo_overflow_frames},
    {"Render underflow (cycles)", &Counters::render_fifo_underflow_cycles},
    {"Render underflow (frames)", &Counters::render_fifo_underflow_frames},
    {"Import conversion failures", &Counters::sample_conversion_import_failures},
    {"Export conversion failures", &Counters::sample_conversion_export_failures},
    {"Virtual ASIO pushed blocks", &Counters::virtual_asio_pushed_blocks},
    {"Virtual ASIO dropped blocks", &Counters::virtual_asio_dropped_blocks},
    {"Virtual ASIO underflows", &Counters::virtual_asio_producer_underflows},
    {"Virtual ASIO overflows", &Counters::virtual_asio_producer_overflows},
    {"Virtual ASIO consumed blocks", &Counters::virtual_asio_consumed_blocks},
    {"Virtual ASIO mixed blocks", &Counters::virtual_asio_mixed_blocks},
    {"Virtual ASIO silent reads", &Counters::virtual_asio_silent_reads},
    {"Virtual ASIO clipped samples", &Counters::virtual_asio_clipped_samples},
    {"Virtual ASIO non-finite samples", &Counters::virtual_asio_non_finite_samples},
    {"Virtual ASIO max queue depth", &Counters::virtual_asio_maximum_queue_depth},
    {"Virtual ASIO active producers", &Counters::virtual_asio_active_producers},
};
struct RecoveryField { const char* label; std::uint64_t Recovery::*member; };
constexpr RecoveryField kRecovery[] = {
    {"Recovery episodes", &Recovery::recovery_episode_count},
    {"Successful recoveries", &Recovery::successful_recovery_count},
    {"Failed recoveries", &Recovery::failed_recovery_count},
    {"Last recovery (ms)", &Recovery::last_recovery_duration_ms},
    {"Maximum recovery (ms)", &Recovery::maximum_recovery_duration_ms},
    {"Notification reopens", &Recovery::endpoint_notification_reopen_count},
    {"Notification reset failures", &Recovery::endpoint_notification_reset_failure_count},
    {"Wait timeout cycles", &Recovery::wait_timeout_cycles},
    {"Capture discontinuity cycles", &Recovery::capture_discontinuity_cycles},
    {"Recovery underflow (frames)", &Recovery::render_fifo_underflow_frames},
    {"Max recovery silence (frames)", &Recovery::maximum_render_recovery_silence_frames},
    {"Max rate-clamped frames", &Recovery::maximum_consecutive_capture_rate_clamped_frames},
};

std::string number(double value, double scale = 1) {
  if (!std::isfinite(value) || !std::isfinite(value * scale)) return "Unknown";
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << std::fixed << std::setprecision(3) << value * scale;
  return out.str();
}

const char* recoveryState(control::WasapiRecoveryState state) {
  switch (state) {
    case control::WasapiRecoveryState::Stopped: return "Stopped";
    case control::WasapiRecoveryState::Opening: return "Opening";
    case control::WasapiRecoveryState::Running: return "Running";
    case control::WasapiRecoveryState::Quiescing: return "Quiescing";
    case control::WasapiRecoveryState::Backoff: return "Backoff";
    case control::WasapiRecoveryState::Faulted: return "Faulted";
  }
  return "Unknown";
}

bool underflow(const Counters& d, const Recovery* r) {
  return d.render_fifo_underflow_cycles || d.render_fifo_underflow_frames ||
      d.virtual_asio_producer_underflows || (r && r->render_fifo_underflow_frames);
}
bool warnings(const Counters& d, const Recovery* r) {
  return underflow(d, r) || d.xrun_count || d.capture_fifo_overflow_cycles ||
      d.capture_fifo_overflow_frames || d.render_fifo_overflow_cycles ||
      d.render_fifo_overflow_frames || d.sample_conversion_import_failures ||
      d.sample_conversion_export_failures || d.virtual_asio_dropped_blocks ||
      d.virtual_asio_producer_overflows || d.virtual_asio_clipped_samples ||
      d.virtual_asio_non_finite_samples || (r && (r->failed_recovery_count ||
      r->endpoint_notification_reset_failure_count || r->wait_timeout_cycles ||
      r->capture_discontinuity_cycles || r->endpoint_notification_reopen_pending ||
      r->runtime_health == control::WasapiRuntimeHealth::Degraded ||
      r->runtime_health == control::WasapiRuntimeHealth::Faulted));
}

class DiagnosticsPanel final : public CViewContainer, public IControlListener {
 public:
  DiagnosticsPanel() : CViewContainer(CRect(0, 0, kWidth, kHeight)) {
    setBackgroundColor(palette::kCanvas);
    text(CRect(24, 16, 400, 44), "DIAGNOSTICS", palette::kSteel);
    refresh_ = makeOwned<CTextButton>(CRect(732, 16, 836, 46), this, 1, "Refresh");
    stylePanelButton(*refresh_);
    refresh_->setTextColor(palette::kText);
    addRetainedView(*this, refresh_);
    status_ = text(CRect(24, 52, 836, 80), "Unknown / awaiting diagnostics");
    freshness_ = text(CRect(24, 80, 836, 104), "", palette::kMuted);
    text(CRect(24, 112, 132, 140), "Counter source", palette::kMuted);
    sources_ = makeOwned<COptionMenu>(CRect(144, 112, 836, 140), this, 2);
    sources_->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    sources_->setFontColor(palette::kText);
    sources_->setBackColor(palette::kRaised);
    sources_->setFrameColor(palette::kLine);
    addRetainedView(*this, sources_);
    for (std::size_t i = 0; i < std::size(kCounters); ++i)
      counters_[i] = row(i, 152, kCounters[i].label);
    callback_ = row(24, 152, "Last callback (ms)");
    peakCallback_ = row(25, 152, "Peak callback (ms)");
    peak_ = row(26, 152, "Virtual ASIO peak (linear)");
    queue_ = row(27, 152, "Endpoint queue fill (frames)");
    correction_ = row(28, 152, "Endpoint correction (ppm)");
    role_ = row(29, 152, "Endpoint role");
    text(CRect(24, 580, 836, 608), "WASAPI RECOVERY", palette::kSteel);
    recovery_ = text(CRect(24, 612, 836, 640), "Unknown");
    reason_ = text(CRect(24, 640, 836, 668), "Unknown", palette::kMuted);
    for (std::size_t i = 0; i < std::size(kRecovery); ++i)
      recoveryCounters_[i] = row(i, 676, kRecovery[i].label);
    message_ = text(CRect(24, 856, 836, 888), "", palette::kWarning);
    text(CRect(24, 892, 836, 920),
         "Reported totals, not interval deltas. Missing telemetry is Unknown; zero is not proof of audio continuity.",
         palette::kMuted);
    renderSources();
    timer_ = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { tick(); }, 100, true);
    inspect();
  }
  ~DiagnosticsPanel() noexcept override { if (timer_) timer_->stop(); }

  void valueChanged(CControl* source) override {
    if (rendering_) return;
    if (source == refresh_.get() && source->getValue() > 0.5F) inspect();
    if (source == sources_.get()) {
      const auto index = sources_->getCurrentIndex();
      selected_ = index > 0 && static_cast<std::size_t>(index) <= snapshot_.response.endpoint_diagnostics.size()
          ? snapshot_.response.endpoint_diagnostics[static_cast<std::size_t>(index - 1)].endpoint_id
          : std::string{};
      render();
    }
  }

 private:
  SharedPointer<CTextLabel> text(const CRect& bounds, const char* value,
                                CColor color = palette::kText) {
    auto label = makeOwned<CTextLabel>(bounds, value);
    label->setFont(makeOwned<CFontDesc>("Segoe UI", 13));
    label->setFontColor(color);
    label->setBackColor(palette::kCanvas);
    label->setFrameColor(palette::kCanvas);
    label->setHoriAlign(kLeftText);
    label->setTextTruncateMode(CTextLabel::kTruncateTail);
    addRetainedView(*this, label);
    return label;
  }
  SharedPointer<CTextLabel> row(std::size_t index, CCoord top, const char* title) {
    const CCoord x = index % 2 ? 440 : 24;
    const CCoord y = top + (index / 2) * 28;
    text(CRect(x, y, x + 232, y + 24), title, palette::kMuted);
    return text(CRect(x + 236, y, x + 396, y + 24), "Unknown");
  }
  void inspect() {
    if (pending_.valid()) return;
    // The worker owns only its value result, never the panel or a UI pointer.
    // A packaged_task future does not join on destruction when the window closes.
    std::packaged_task<Snapshot()> task(readDiagnostics);
    auto future = task.get_future();
    try {
      std::thread(std::move(task)).detach();
      pending_ = std::move(future);
      setPanelControlEnabled(*refresh_, false);
    } catch (...) {
      snapshot_ = {};
      snapshot_.error = "Could not start diagnostics worker";
      nextPoll_ = Clock::now() + std::chrono::seconds(2);
      renderSources();
    }
  }
  void tick() {
    if (pending_.valid() && pending_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      try { snapshot_ = pending_.get(); }
      catch (...) { snapshot_ = {}; snapshot_.error = "Diagnostics worker failed"; }
      received_ = Clock::now();
      nextPoll_ = received_ + std::chrono::seconds(2);
      setPanelControlEnabled(*refresh_, true);
      renderSources();
    }
    if (!pending_.valid() && Clock::now() >= nextPoll_) inspect();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - received_).count();
    const auto age = snapshot_.valid ? "Snapshot age: " + std::to_string(seconds) + " s" : "No current counter snapshot";
    freshness_->setText((age + (pending_.valid() ? " / Reading..." : " / Polling every 2 s")).c_str());
  }
  void renderSources() {
    rendering_ = true;
    sources_->removeAllEntry();
    sources_->addEntry("Engine aggregate");
    int selected = 0, index = 0;
    for (const auto& endpoint : snapshot_.response.endpoint_diagnostics) {
      sources_->addEntry(endpoint.endpoint_id.c_str());
      ++index;
      if (endpoint.endpoint_id == selected_) selected = index;
    }
    // Preserve the selected identity through outages; do not silently substitute aggregate counters.
    if (!selected_.empty() && selected == 0) {
      sources_->addEntry("Selected endpoint unavailable");
      selected = index + 1;
    }
    sources_->setCurrent(selected);
    rendering_ = false;
    render();
  }
  void render() {
    const auto& response = snapshot_.response;
    const Counters* d = snapshot_.valid && selected_.empty() ? &response.diagnostics : nullptr;
    const Recovery* r = d && response.has_wasapi_recovery ? &response.wasapi_recovery : nullptr;
    const control::AudioEndpointRuntimeDiagnostics* endpoint = nullptr;
    if (snapshot_.valid && !selected_.empty()) {
      for (const auto& item : response.endpoint_diagnostics) if (item.endpoint_id == selected_) {
        endpoint = &item; d = &item.diagnostics; r = item.recovery ? &*item.recovery : nullptr; break;
      }
    }
    bool hasUnderflow = d && underflow(*d, r), hasWarning = d && warnings(*d, r);
    if (d && selected_.empty()) for (const auto& item : response.endpoint_diagnostics) {
      const auto recovery = item.recovery ? &*item.recovery : nullptr;
      hasUnderflow = hasUnderflow || underflow(item.diagnostics, recovery);
      hasWarning = hasWarning || warnings(item.diagnostics, recovery);
    }
    const char* status = snapshot_.offline ? "Offline / engine unreachable; counters unknown"
        : !d ? "Unknown / diagnostics unavailable"
        : hasUnderflow ? "Warning / recorded underflows"
        : hasWarning ? "Warning / recorded errors or recovery warning"
        : !r ? "Counters received / recovery health unknown"
        : "Counters received / no recorded counter warnings";
    status_->setText(status);
    status_->setFontColor(snapshot_.offline ? palette::kDanger : (!d || hasWarning || !r) ? palette::kWarning : palette::kText);
    for (std::size_t i = 0; i < std::size(kCounters); ++i)
      counters_[i]->setText(d ? std::to_string(d->*kCounters[i].member).c_str() : "Unknown");
    callback_->setText(d ? number(d->last_callback_seconds, 1000).c_str() : "Unknown");
    peakCallback_->setText(d ? number(d->peak_callback_seconds, 1000).c_str() : "Unknown");
    peak_->setText(d ? number(d->virtual_asio_peak).c_str() : "Unknown");
    queue_->setText(endpoint && endpoint->queue_fill_frames ? std::to_string(*endpoint->queue_fill_frames).c_str() : "Unknown");
    correction_->setText(endpoint && endpoint->correction_ppm ? number(*endpoint->correction_ppm).c_str() : "Unknown");
    role_->setText(endpoint ? (endpoint->role == control::AudioEndpointRuntimeRole::Master ? "Master" : "Follower") : "Not applicable");
    std::string recoveryText = "Unknown / recovery telemetry unavailable";
    if (r) {
      recoveryText = recoveryState(r->state);
      switch (r->runtime_health) {
        case control::WasapiRuntimeHealth::Stopped: recoveryText += " / Engine reports stopped"; break;
        case control::WasapiRuntimeHealth::Healthy: recoveryText += " / Engine reports healthy"; break;
        case control::WasapiRuntimeHealth::Degraded: recoveryText += " / Engine reports degraded"; break;
        case control::WasapiRuntimeHealth::Faulted: recoveryText += " / Engine reports faulted"; break;
        default: recoveryText += " / Engine health unknown"; break;
      }
      if (r->endpoint_notification_reopen_pending) recoveryText += " / Reopen pending";
    }
    recovery_->setText(recoveryText.c_str());
    recovery_->setFontColor(r && hasWarning ? palette::kWarning : palette::kText);
    reason_->setText(r ? ("Reason: " + (r->runtime_reason_code.empty() ? std::string("Not supplied") : r->runtime_reason_code)).c_str() : "Reason: Unknown");
    for (std::size_t i = 0; i < std::size(kRecovery); ++i)
      recoveryCounters_[i]->setText(r ? std::to_string(r->*kRecovery[i].member).c_str() : "Unknown");
    message_->setText(snapshot_.error.c_str());
  }

  Snapshot snapshot_;
  std::string selected_;
  bool rendering_ = false;
  std::future<Snapshot> pending_;
  Clock::time_point received_ = Clock::now(), nextPoll_ = Clock::now();
  SharedPointer<CVSTGUITimer> timer_;
  SharedPointer<CTextButton> refresh_;
  SharedPointer<COptionMenu> sources_;
  SharedPointer<CTextLabel> status_, freshness_, callback_, peakCallback_, peak_, queue_, correction_, role_;
  SharedPointer<CTextLabel> recovery_, reason_, message_;
  std::array<SharedPointer<CTextLabel>, std::size(kCounters)> counters_;
  std::array<SharedPointer<CTextLabel>, std::size(kRecovery)> recoveryCounters_;
};

struct LastDiagnosticsWindow final : WindowListenerAdapter {
  void onClosed(const IWindow&) override {
    if (IApplication::instance().getWindows().empty()) IApplication::instance().quit();
  }
};
LastDiagnosticsWindow lastWindow;
}  // namespace

void showDiagnosticsWindow() {
  for (const auto& window : IApplication::instance().getWindows()) {
    if (window->getTitle().getString() == kTitle) { window->show(); window->activate(); return; }
  }
  WindowConfiguration config;
  config.type = WindowType::Document;
  config.style.border().close().size();
  config.size = CPoint(kWidth, kViewportHeight);
  config.title = kTitle;
  auto window = IApplication::instance().createWindow(config, std::make_shared<WindowControllerAdapter>());
  if (!window) return;
  auto frame = makeOwned<CFrame>(CRect(0, 0, kWidth, kViewportHeight), nullptr);
  frame->setFocusDrawingEnabled(true);
  frame->setFocusColor(palette::kSteel);
  auto viewport = makeOwned<ThemedScrollView>(CRect(0, 0, kWidth, kViewportHeight),
      CRect(0, 0, kWidth, kHeight), CScrollView::kHorizontalScrollbar |
      CScrollView::kVerticalScrollbar | CScrollView::kAutoHideScrollbars |
      CScrollView::kDontDrawFrame | CScrollView::kFollowFocusView);
  viewport->setBackgroundColor(palette::kCanvas);
  styleScrollbars(*viewport);
  viewport->setAutosizeFlags(kAutosizeLeft | kAutosizeTop | kAutosizeRight | kAutosizeBottom);
  addRetainedView(*viewport, makeOwned<DiagnosticsPanel>());
  addRetainedView(*frame, viewport);
  window->setContentView(frame);
  prepareScrollableWindow(*window, *frame, CPoint(kWidth, kViewportHeight),
      MonitorFromWindow(GetForegroundWindow(), MONITOR_DEFAULTTONEAREST));
  window->registerWindowListener(&lastWindow);
  window->show();
}

}  // namespace sar::gui_vstgui
