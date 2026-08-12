#include "app/gui/engine_controller.h"

#include "core/control/control_wire_protocol.h"
#include "core/service/windows_named_pipe_control.h"

#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <QtConcurrentRun>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace sar::gui {
namespace {

std::vector<std::byte> as_bytes(const std::vector<std::uint8_t>& input) {
  std::vector<std::byte> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = static_cast<std::byte>(input[index]);
  }
  return result;
}

std::vector<std::uint8_t> as_u8(std::span<const std::byte> input) {
  std::vector<std::uint8_t> result(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    result[index] = std::to_integer<std::uint8_t>(input[index]);
  }
  return result;
}

QString text(const std::string& value) {
  return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::uint32_t transaction_timeout_ms(
    control::ControlCommandType command_type) noexcept {
  if (control::control_command_mutates_preset(command_type) ||
      command_type == control::ControlCommandType::ConfigureAudioRuntime ||
      command_type == control::ControlCommandType::StartAudioRuntime ||
      command_type == control::ControlCommandType::StopAudioRuntime) {
    return 5000;
  }
  return 2000;
}

EngineReply transact(control::ControlCommand command) {
  EngineReply reply;
  reply.request_type = command.type;
  const auto encoded = control::encode_control_command(command);
  if (!encoded.ok()) {
    reply.error = QStringLiteral("Could not encode the control request");
    return reply;
  }

  service::NamedPipeControlConfig config;
  const auto transaction = service::transact_named_pipe_control(
      config, as_bytes(encoded.bytes), transaction_timeout_ms(command.type));
  if (!transaction.ok()) {
    reply.delivery_uncertain =
        transaction.error().code == "pipe_read_timeout";
    reply.error = reply.delivery_uncertain
                      ? QStringLiteral(
                            "The engine response timed out; confirming the "
                            "current state")
                      : text(transaction.error().message);
    return reply;
  }
  const auto decoded =
      control::decode_control_response(as_u8(transaction.payload()));
  if (!decoded.ok()) {
    reply.error = QStringLiteral("The engine returned an invalid response");
    return reply;
  }
  reply.transport_ok = true;
  reply.response = decoded.response;
  if (reply.response.status == control::ControlResponseStatus::Rejected) {
    if (reply.response.errors.empty()) {
      reply.error = QStringLiteral("The engine rejected the request");
    } else {
      QStringList errors;
      errors.reserve(static_cast<qsizetype>(reply.response.errors.size()));
      for (const auto& error : reply.response.errors) {
        errors.push_back(QStringLiteral("%1: %2")
                             .arg(text(error.code), text(error.message)));
      }
      reply.error = errors.join(QStringLiteral("; "));
    }
  }
  return reply;
}

QVariantMap endpoint(const graph::RouteEndpointDescriptor& value) {
  return {{QStringLiteral("id"), text(value.id)},
          {QStringLiteral("label"), text(value.label)}};
}

}  // namespace

EngineController::EngineController(QObject* parent)
    : EngineController(transact, true, parent) {}

EngineController::EngineController(EngineTransport transport,
                                   bool automatic_activity,
                                   QObject* parent)
    : QObject(parent),
      transport_(std::move(transport)),
      preset_store_(QStandardPaths::writableLocation(
                        QStandardPaths::AppDataLocation) +
                     QStringLiteral("/presets")),
      command_prefix_(
          "gui-" +
          QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() +
          "-"),
      service_management_enabled_(automatic_activity) {
  connect(&watcher_, &QFutureWatcher<EngineReply>::finished, this, [this] {
    const auto reply = watcher_.result();
    if (!active_command_.has_value()) {
      updateBusyState();
      return;
    }
    const auto command = std::move(*active_command_);
    active_command_.reset();
    applyReply(reply, command);
    startNextCommand();
    updateBusyState();
  });
  connect(&engine_service_, &QProcess::started, this, [this] {
    if (shutting_down_) {
      return;
    }
    engine_service_owned_ = true;
    setStatus(QStringLiteral("Engine service started"));
    QTimer::singleShot(100, this, &EngineController::refresh);
  });
  connect(&engine_service_, &QProcess::errorOccurred, this,
          [this](QProcess::ProcessError error) {
            if (shutting_down_) {
              return;
            }
            if (error == QProcess::FailedToStart) {
              engine_service_owned_ = false;
              engine_service_start_attempted_ = false;
              setError(QStringLiteral("Could not start the engine service: %1")
                           .arg(engine_service_.errorString()));
            }
          });
  connect(&engine_service_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [this](int exit_code, QProcess::ExitStatus exit_status) {
            const bool was_owned = engine_service_owned_;
            engine_service_owned_ = false;
            engine_service_start_attempted_ = false;
            if (!shutting_down_ && was_owned) {
              const auto reason = exit_status == QProcess::CrashExit
                                      ? QStringLiteral("crashed")
                                      : QStringLiteral("exited");
              setError(QStringLiteral("Engine service %1 (code %2)")
                           .arg(reason)
                           .arg(exit_code));
            }
          });
  // Diagnostics feed the meter at control rate; slower queries stay throttled
  // independently in schedulePoll().
  poll_timer_.setInterval(50);
  connect(&poll_timer_, &QTimer::timeout, this,
          &EngineController::schedulePoll);
  if (automatic_activity) {
    poll_timer_.start();
  }
  refreshPresets();
  if (automatic_activity) {
    QTimer::singleShot(0, this, &EngineController::refresh);
  }
}

EngineController::~EngineController() {
  poll_timer_.stop();
  shutting_down_ = true;
  stopEngineService();
}

bool EngineController::connected() const noexcept { return connected_; }
QString EngineController::connectionLabel() const {
  return connected_ ? QStringLiteral("Engine online")
                    : QStringLiteral("Engine offline");
}
QString EngineController::lastError() const { return last_error_; }
QString EngineController::statusMessage() const { return status_message_; }
bool EngineController::runtimeRunning() const noexcept { return runtime_running_; }
bool EngineController::runtimeConfigured() const noexcept {
  return runtime_configured_;
}
QString EngineController::runtimeMode() const {
  switch (runtime_mode_) {
    case control::AudioRuntimeMode::WasapiRender:
      return QStringLiteral("render");
    case control::AudioRuntimeMode::WasapiDuplex:
      return QStringLiteral("duplex");
    case control::AudioRuntimeMode::None:
      return QStringLiteral("none");
  }
  return QStringLiteral("none");
}
QString EngineController::runtimeCaptureDeviceId() const {
  return runtime_capture_device_id_;
}
QString EngineController::runtimeRenderDeviceId() const {
  return runtime_render_device_id_;
}
bool EngineController::busy() const noexcept { return busy_; }
int EngineController::sampleRate() const noexcept { return sample_rate_; }
int EngineController::blockSize() const noexcept { return block_size_; }
qulonglong EngineController::graphVersion() const noexcept { return graph_version_; }
qulonglong EngineController::xrunCount() const noexcept { return xrun_count_; }
qulonglong EngineController::droppedBlocks() const noexcept { return dropped_blocks_; }
qulonglong EngineController::virtualAsioProducerUnderflows() const noexcept {
  return virtual_asio_producer_underflows_;
}
qulonglong EngineController::virtualAsioProducerOverflows() const noexcept {
  return virtual_asio_producer_overflows_;
}
int EngineController::activeClients() const noexcept { return active_clients_; }
double EngineController::peak() const noexcept { return peak_; }
double EngineController::callbackPeakUs() const noexcept { return callback_peak_us_; }
bool EngineController::wasapiRecoveryAvailable() const noexcept {
  return wasapi_recovery_available_;
}
QString EngineController::wasapiRecoveryState() const {
  switch (wasapi_recovery_.state) {
    case control::WasapiRecoveryState::Stopped:
      return QStringLiteral("Stopped");
    case control::WasapiRecoveryState::Opening:
      return QStringLiteral("Opening");
    case control::WasapiRecoveryState::Running:
      return QStringLiteral("Running");
    case control::WasapiRecoveryState::Quiescing:
      return QStringLiteral("Quiescing");
    case control::WasapiRecoveryState::Backoff:
      return QStringLiteral("Backoff");
    case control::WasapiRecoveryState::Faulted:
      return QStringLiteral("Faulted");
  }
  return QStringLiteral("Unknown");
}
QString EngineController::wasapiRuntimeHealth() const {
  switch (wasapi_recovery_.runtime_health) {
    case control::WasapiRuntimeHealth::Stopped:
      return QStringLiteral("Stopped");
    case control::WasapiRuntimeHealth::Healthy:
      return QStringLiteral("Healthy");
    case control::WasapiRuntimeHealth::Degraded:
      return QStringLiteral("Degraded");
    case control::WasapiRuntimeHealth::Faulted:
      return QStringLiteral("Faulted");
  }
  return QStringLiteral("Unknown");
}
QString EngineController::wasapiRuntimeReasonCode() const {
  return text(wasapi_recovery_.runtime_reason_code);
}
qulonglong EngineController::wasapiRecoveryEpisodes() const noexcept {
  return wasapi_recovery_.recovery_episode_count;
}
qulonglong EngineController::wasapiSuccessfulRecoveries() const noexcept {
  return wasapi_recovery_.successful_recovery_count;
}
qulonglong EngineController::wasapiFailedRecoveries() const noexcept {
  return wasapi_recovery_.failed_recovery_count;
}
qulonglong EngineController::wasapiLastRecoveryMs() const noexcept {
  return wasapi_recovery_.last_recovery_duration_ms;
}
qulonglong EngineController::wasapiMaximumRecoveryMs() const noexcept {
  return wasapi_recovery_.maximum_recovery_duration_ms;
}
qulonglong EngineController::wasapiEndpointReopens() const noexcept {
  return wasapi_recovery_.endpoint_notification_reopen_count;
}
qulonglong EngineController::wasapiEndpointResetFailures() const noexcept {
  return wasapi_recovery_.endpoint_notification_reset_failure_count;
}
bool EngineController::wasapiEndpointReopenPending() const noexcept {
  return wasapi_recovery_.endpoint_notification_reopen_pending;
}
qulonglong EngineController::wasapiWaitTimeoutCycles() const noexcept {
  return wasapi_recovery_.wait_timeout_cycles;
}
qulonglong EngineController::wasapiCaptureDiscontinuityCycles() const noexcept {
  return wasapi_recovery_.capture_discontinuity_cycles;
}
qulonglong EngineController::wasapiRenderFifoUnderflowFrames() const noexcept {
  return wasapi_recovery_.render_fifo_underflow_frames;
}
qulonglong EngineController::wasapiMaximumRenderRecoverySilenceFrames()
    const noexcept {
  return wasapi_recovery_.maximum_render_recovery_silence_frames;
}
qulonglong
EngineController::wasapiMaximumConsecutiveCaptureRateClampedFrames()
    const noexcept {
  return wasapi_recovery_.maximum_consecutive_capture_rate_clamped_frames;
}
QVariantList EngineController::inputs() const { return inputs_; }
QVariantList EngineController::outputs() const { return outputs_; }
QVariantList EngineController::routes() const { return routes_; }
QVariantList EngineController::devices() const { return devices_; }
int EngineController::routeRevision() const noexcept { return route_revision_; }
QStringList EngineController::presetNames() const { return preset_names_; }
QString EngineController::activePresetName() const {
  return active_preset_name_;
}
bool EngineController::canUndo() const noexcept { return !undo_history_.empty(); }
bool EngineController::canRedo() const noexcept { return !redo_history_.empty(); }

void EngineController::refresh() {
  control::ControlCommand command;
  command.type = control::ControlCommandType::QuerySessionState;
  dispatch(std::move(command));
}

void EngineController::refreshPresets() {
  QString error;
  const auto names = preset_store_.names(&error);
  if (!error.isEmpty()) {
    setError(std::move(error));
    return;
  }
  if (preset_names_ != names) {
    preset_names_ = names;
    emit presetsChanged();
  }
}

void EngineController::savePreset(const QString& name) {
  const auto normalized = name.trimmed();
  QString error;
  if (!PresetStore::validName(normalized, &error)) {
    setError(std::move(error));
    return;
  }
  if (busy_) {
    setError(QStringLiteral("Wait for the current engine command to finish"));
    return;
  }
  clearFeedback();
  control::ControlCommand command;
  command.type = control::ControlCommandType::SavePreset;
  dispatchPreset(std::move(command), PendingPresetAction::Save, normalized);
}

void EngineController::loadPreset(const QString& name) {
  const auto normalized = name.trimmed();
  if (busy_) {
    setError(QStringLiteral("Wait for the current engine command to finish"));
    return;
  }
  control::PresetDocument preset;
  QString error;
  if (!preset_store_.load(normalized, &preset, &error)) {
    setError(std::move(error));
    return;
  }
  clearFeedback();
  control::ControlCommand command;
  command.type = control::ControlCommandType::LoadPreset;
  command.preset = std::move(preset);
  enqueue({std::move(command), PendingPresetAction::Load, normalized, false,
           HistoryAction::Reset});
}

void EngineController::clearFeedback() {
  if (last_error_.isEmpty() && status_message_.isEmpty()) {
    return;
  }
  last_error_.clear();
  status_message_.clear();
  emit feedbackChanged();
}

void EngineController::startRuntime() {
  control::ControlCommand command;
  command.type = control::ControlCommandType::StartAudioRuntime;
  dispatch(std::move(command));
}

void EngineController::stopRuntime() {
  control::ControlCommand command;
  command.type = control::ControlCommandType::StopAudioRuntime;
  dispatch(std::move(command));
}

void EngineController::configureAudioRuntime(const QString& mode,
                                             const QString& capture_device_id,
                                             const QString& render_device_id) {
  control::AudioRuntimeMode runtime_mode = control::AudioRuntimeMode::None;
  if (mode == QStringLiteral("render")) {
    runtime_mode = control::AudioRuntimeMode::WasapiRender;
  } else if (mode == QStringLiteral("duplex")) {
    runtime_mode = control::AudioRuntimeMode::WasapiDuplex;
  } else {
    setError(QStringLiteral("Choose a WASAPI runtime mode"));
    return;
  }

  if (render_device_id.trimmed().isEmpty() ||
      (runtime_mode == control::AudioRuntimeMode::WasapiDuplex &&
       capture_device_id.trimmed().isEmpty())) {
    setError(QStringLiteral("Select the required audio devices"));
    return;
  }

  control::ControlCommand command;
  command.type = control::ControlCommandType::ConfigureAudioRuntime;
  command.audio_runtime.mode = runtime_mode;
  command.audio_runtime.capture_device_id =
      runtime_mode == control::AudioRuntimeMode::WasapiDuplex
          ? capture_device_id.toStdString()
          : std::string{};
  command.audio_runtime.render_device_id = render_device_id.toStdString();
  dispatch(std::move(command));
}

bool EngineController::routeEnabled(const QString& input_id,
                                    const QString& output_id) const {
  return std::ranges::any_of(routes_, [&](const QVariant& value) {
    const auto route = value.toMap();
    return route.value(QStringLiteral("inputId")) == input_id &&
           route.value(QStringLiteral("outputId")) == output_id &&
           !route.value(QStringLiteral("muted")).toBool();
  });
}

double EngineController::routeGain(const QString& input_id,
                                   const QString& output_id) const {
  for (const auto& value : routes_) {
    const auto route = value.toMap();
    if (route.value(QStringLiteral("inputId")) == input_id &&
        route.value(QStringLiteral("outputId")) == output_id) {
      return route.value(QStringLiteral("gain")).toDouble();
    }
  }
  return 0.0;
}

void EngineController::setRoute(const QString& input_id,
                                const QString& output_id,
                                bool enabled) {
  if (busy_) {
    setError(QStringLiteral("Wait for the current engine command to finish"));
    return;
  }
  control::ControlCommand command;
  auto input = input_id.toStdString();
  auto output = output_id.toStdString();
  const control::PresetRoute* existing_route = nullptr;
  if (current_preset_.has_value()) {
    const auto route = std::ranges::find_if(
        current_preset_->matrix.routes, [&](const auto& candidate) {
          return candidate.input_id == input && candidate.output_id == output;
        });
    if (route != current_preset_->matrix.routes.end()) {
      existing_route = &*route;
    }
  }

  if (existing_route != nullptr) {
    if (existing_route->muted == !enabled) {
      return;
    }
    command.type = control::ControlCommandType::SetMute;
    command.mute = !enabled;
  } else if (!enabled && existing_route == nullptr) {
    return;
  } else {
    command.type = control::ControlCommandType::ConnectRoute;
  }
  command.input_id = std::move(input);
  command.output_id = std::move(output);
  command.gain = 1.0F;
  QueuedCommand queued{command};
  if (current_preset_.has_value()) {
    auto preview = command;
    preview.command_id = "gui-history-preview";
    auto applied = control::apply_command(*current_preset_, preview);
    if (applied.ok()) {
      queued.history_action = HistoryAction::Record;
      queued.history_entry = HistoryEntry{*current_preset_,
                                          applied.take_document()};
    }
  }
  enqueue(std::move(queued));
}

void EngineController::setRouteGain(const QString& input_id,
                                    const QString& output_id,
                                    double gain) {
  control::ControlCommand command;
  command.type = control::ControlCommandType::SetGain;
  command.input_id = input_id.toStdString();
  command.output_id = output_id.toStdString();
  command.gain = static_cast<float>(std::clamp(gain, 0.0, 4.0));
  QueuedCommand queued{command};
  if (current_preset_.has_value()) {
    auto preview = command;
    preview.command_id = "gui-history-preview";
    auto applied = control::apply_command(*current_preset_, preview);
    if (applied.ok()) {
      queued.history_action = HistoryAction::Record;
      queued.history_entry = HistoryEntry{*current_preset_,
                                          applied.take_document()};
    }
  }
  enqueue(std::move(queued));
}

void EngineController::undo() {
  if (busy_ || undo_history_.empty()) {
    return;
  }
  dispatchHistoryLoad(undo_history_.back(), HistoryAction::Undo);
}

void EngineController::redo() {
  if (busy_ || redo_history_.empty()) {
    return;
  }
  dispatchHistoryLoad(redo_history_.back(), HistoryAction::Redo);
}

void EngineController::dispatch(control::ControlCommand command) {
  enqueue({std::move(command)});
}

void EngineController::enqueue(QueuedCommand queued) {
  if (queued.command.type == control::ControlCommandType::SetGain) {
    for (auto it = queued_commands_.rbegin(); it != queued_commands_.rend(); ++it) {
      if (it->command.type == control::ControlCommandType::SetGain &&
          it->command.input_id == queued.command.input_id &&
          it->command.output_id == queued.command.output_id) {
        it->command.gain = queued.command.gain;
        if (it->history_entry.has_value() && queued.history_entry.has_value()) {
          it->history_entry->after = queued.history_entry->after;
        }
        return;
      }
    }
  }
  queued_commands_.push_back(std::move(queued));
  startNextCommand();
  updateBusyState();
}

void EngineController::startNextCommand() {
  if (active_command_.has_value() || queued_commands_.empty()) {
    return;
  }
  active_command_ = std::move(queued_commands_.front());
  queued_commands_.pop_front();
  auto command = active_command_->command;
  command.command_id = command_prefix_ + std::to_string(++command_sequence_);
  auto transport = transport_;
  watcher_.setFuture(QtConcurrent::run(
      [transport = std::move(transport), command = std::move(command)]() mutable {
    return transport(std::move(command));
  }));
}

void EngineController::updateBusyState() {
  const auto is_user_command = [](const QueuedCommand& command) {
    return !command.poll;
  };
  const bool should_be_busy =
      (active_command_.has_value() && is_user_command(*active_command_)) ||
      std::ranges::any_of(queued_commands_, is_user_command);
  if (busy_ == should_be_busy) {
    return;
  }
  busy_ = should_be_busy;
  emit busyChanged();
}

void EngineController::dispatchPreset(control::ControlCommand command,
                                      PendingPresetAction action,
                                      QString name) {
  enqueue({std::move(command), action, std::move(name)});
}

void EngineController::dispatchHistoryLoad(const HistoryEntry& entry,
                                           HistoryAction action) {
  control::ControlCommand command;
  command.type = control::ControlCommandType::LoadPreset;
  command.preset = action == HistoryAction::Undo ? entry.before : entry.after;
  enqueue({std::move(command), PendingPresetAction::None, {}, false, action,
           entry});
}

void EngineController::pushBounded(std::deque<HistoryEntry>& history,
                                   HistoryEntry entry) {
  if (history.size() == kHistoryLimit) {
    history.pop_front();
  }
  history.push_back(std::move(entry));
}

void EngineController::commitHistory(const QueuedCommand& command) {
  const bool had_undo = canUndo();
  const bool had_redo = canRedo();
  switch (command.history_action) {
    case HistoryAction::None:
      return;
    case HistoryAction::Record:
      if (command.history_entry.has_value()) {
        pushBounded(undo_history_, *command.history_entry);
        redo_history_.clear();
        current_preset_ = command.history_entry->after;
      }
      break;
    case HistoryAction::Undo:
      if (command.history_entry.has_value() && !undo_history_.empty()) {
        auto entry = std::move(undo_history_.back());
        undo_history_.pop_back();
        pushBounded(redo_history_, std::move(entry));
        current_preset_ = command.history_entry->before;
      }
      break;
    case HistoryAction::Redo:
      if (command.history_entry.has_value() && !redo_history_.empty()) {
        auto entry = std::move(redo_history_.back());
        redo_history_.pop_back();
        pushBounded(undo_history_, std::move(entry));
        current_preset_ = command.history_entry->after;
      }
      break;
    case HistoryAction::Reset:
      undo_history_.clear();
      redo_history_.clear();
      current_preset_ = command.command.preset;
      break;
  }
  if (had_undo != canUndo() || had_redo != canRedo()) {
    emit historyChanged();
  }
  if (current_preset_.has_value()) {
    updatePresetView(*current_preset_);
    emit sessionChanged();
  }
}

void EngineController::applyReply(const EngineReply& reply,
                                  const QueuedCommand& command) {
  const bool command_succeeded =
      reply.transport_ok &&
      reply.error.isEmpty() &&
      reply.response.status != control::ControlResponseStatus::Rejected;
  const bool was_connected = connected_;
  if (!reply.delivery_uncertain) {
    connected_ = reply.transport_ok;
  }
  if (was_connected != connected_) {
    emit connectionChanged();
  }
  if (!reply.delivery_uncertain && !reply.transport_ok) {
    connection_error_active_ = true;
  } else if (!was_connected && connected_ && connection_error_active_) {
    connection_error_active_ = false;
    if (reply.error.isEmpty()) {
      clearFeedback();
    }
  }
  if (!reply.error.isEmpty()) {
    setError(reply.error);
  }
  if (!command_succeeded) {
    if (reply.delivery_uncertain) {
      QTimer::singleShot(0, this, &EngineController::refresh);
    } else if (!reply.transport_ok && service_management_enabled_) {
      ensureEngineService();
    }
    return;
  }

  commitHistory(command);

  if (reply.response.has_session_state || reply.response.has_preset ||
      reply.response.has_devices || reply.response.has_active_graph) {
    updateSession(reply.response);
  }
  if (reply.response.has_audio_runtime_state) {
    runtime_running_ = reply.response.audio_runtime.running;
    runtime_configured_ = reply.response.audio_runtime.configured;
    runtime_mode_ = reply.response.audio_runtime.configuration.mode;
    runtime_capture_device_id_ =
        text(reply.response.audio_runtime.configuration.capture_device_id);
    runtime_render_device_id_ =
        text(reply.response.audio_runtime.configuration.render_device_id);
    graph_version_ = reply.response.audio_runtime.graph_version;
    emit runtimeChanged();
    emit sessionChanged();
  }
  bool diagnostics_changed = false;
  if (reply.response.has_diagnostics) {
    const auto& diagnostics = reply.response.diagnostics;
    xrun_count_ = diagnostics.xrun_count;
    dropped_blocks_ = diagnostics.virtual_asio_dropped_blocks;
    virtual_asio_producer_underflows_ =
        diagnostics.virtual_asio_producer_underflows;
    virtual_asio_producer_overflows_ =
        diagnostics.virtual_asio_producer_overflows;
    active_clients_ =
        static_cast<int>(diagnostics.virtual_asio_active_producers);
    peak_ = diagnostics.virtual_asio_peak;
    callback_peak_us_ = diagnostics.peak_callback_seconds * 1'000'000.0;
    diagnostics_changed = true;
  }
  if (reply.response.has_wasapi_recovery) {
    wasapi_recovery_available_ = true;
    wasapi_recovery_ = reply.response.wasapi_recovery;
    diagnostics_changed = true;
  } else if (reply.response.has_diagnostics && wasapi_recovery_available_) {
    wasapi_recovery_available_ = false;
    wasapi_recovery_ = {};
    diagnostics_changed = true;
  }
  if (diagnostics_changed) {
    emit diagnosticsChanged();
  }

  if (command.preset_action == PendingPresetAction::Save) {
    QString error;
    if (!reply.response.has_preset) {
      setError(QStringLiteral("The engine did not return a preset document"));
    } else if (!preset_store_.save(command.preset_name,
                                   reply.response.preset, &error)) {
      setError(std::move(error));
    } else {
      active_preset_name_ = command.preset_name;
      refreshPresets();
      emit presetsChanged();
      setStatus(QStringLiteral("Saved preset \"%1\"")
                    .arg(active_preset_name_));
    }
  } else if (command.preset_action == PendingPresetAction::Load) {
    active_preset_name_ = command.preset_name;
    emit presetsChanged();
    setStatus(QStringLiteral("Loaded preset \"%1\"")
                  .arg(active_preset_name_));
  }
  if (reply.request_type == control::ControlCommandType::ConnectRoute ||
      reply.request_type == control::ControlCommandType::DisconnectRoute ||
      reply.request_type == control::ControlCommandType::SetGain ||
      reply.request_type == control::ControlCommandType::SetMute ||
      reply.request_type == control::ControlCommandType::LoadPreset ||
      reply.request_type == control::ControlCommandType::StartAudioRuntime ||
      reply.request_type == control::ControlCommandType::StopAudioRuntime) {
    QTimer::singleShot(0, this, &EngineController::refresh);
  }
}

void EngineController::updateSession(const control::ControlResponse& response) {
  if (response.has_preset || response.has_session_state) {
    updatePresetView(response.preset);
  }
  if (response.has_devices || response.has_session_state) {
    devices_.clear();
    for (const auto& device : response.devices) {
      devices_.push_back(QVariantMap{
          {QStringLiteral("id"), text(device.id)},
          {QStringLiteral("label"), text(device.label)},
          {QStringLiteral("isDefault"), device.is_default},
          {QStringLiteral("isVirtual"), device.is_virtual},
          {QStringLiteral("isWasapi"),
           device.backend == platform::AudioBackendKind::Wasapi},
          {QStringLiteral("direction"), static_cast<int>(device.direction)},
      });
    }
  }
  if (response.has_active_graph) {
    graph_version_ = response.active_graph.version;
  }
  emit sessionChanged();
}

void EngineController::updatePresetView(
    const control::PresetDocument& preset) {
  current_preset_ = preset;
  sample_rate_ = static_cast<int>(preset.sample_rate);
  block_size_ = static_cast<int>(preset.frames_per_block);
  inputs_.clear();
  outputs_.clear();
  routes_.clear();
  for (const auto& input : preset.matrix.inputs) {
    inputs_.push_back(endpoint(input));
  }
  for (const auto& output : preset.matrix.outputs) {
    outputs_.push_back(endpoint(output));
  }
  for (const auto& route : preset.matrix.routes) {
    routes_.push_back(QVariantMap{
        {QStringLiteral("inputId"), text(route.input_id)},
        {QStringLiteral("outputId"), text(route.output_id)},
        {QStringLiteral("gain"), route.gain},
        {QStringLiteral("muted"), route.muted},
    });
  }
  ++route_revision_;
}

void EngineController::setError(QString error) {
  if (error.isEmpty()) {
    return;
  }
  const bool changed =
      last_error_ != error || !status_message_.isEmpty();
  last_error_ = std::move(error);
  status_message_.clear();
  if (changed) {
    emit feedbackChanged();
  }
}

void EngineController::setStatus(QString status) {
  const bool changed =
      status_message_ != status || !last_error_.isEmpty();
  status_message_ = std::move(status);
  last_error_.clear();
  if (changed) {
    emit feedbackChanged();
  }
}

void EngineController::schedulePoll() {
  if (active_command_.has_value() || !queued_commands_.empty()) {
    return;
  }
  control::ControlCommand command;
  ++poll_sequence_;
  if (poll_sequence_ % 100 == 0) {
    command.type = control::ControlCommandType::QuerySessionState;
  } else if (poll_sequence_ % 20 == 0) {
    command.type = control::ControlCommandType::QueryAudioRuntime;
  } else {
    command.type = control::ControlCommandType::QueryDiagnostics;
  }
  enqueue({std::move(command), PendingPresetAction::None, {}, true});
}

void EngineController::ensureEngineService() {
  if (engine_service_start_attempted_ ||
      engine_service_.state() != QProcess::NotRunning) {
    return;
  }
  engine_service_start_attempted_ = true;
  const auto executable = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("sar_engine_service.exe"));
  if (!QFileInfo::exists(executable)) {
    engine_service_start_attempted_ = false;
    setError(QStringLiteral("The engine service executable is missing from this installation"));
    return;
  }
  const auto data_path = QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation);
  if (data_path.isEmpty() || !QDir().mkpath(data_path)) {
    engine_service_start_attempted_ = false;
    setError(QStringLiteral("Could not create the engine service data directory"));
    return;
  }
  engine_service_.setProgram(executable);
  engine_service_.setArguments({QStringLiteral("--session"),
                                QDir(data_path).filePath(
                                    QStringLiteral("engine-session.sarsession"))});
  engine_service_.setProcessChannelMode(QProcess::MergedChannels);
  setStatus(QStringLiteral("Starting engine service"));
  engine_service_.start();
}

void EngineController::stopEngineService() {
  if (engine_service_.state() == QProcess::NotRunning) {
    return;
  }
  engine_service_.terminate();
  if (!engine_service_.waitForFinished(1500)) {
    engine_service_.kill();
    engine_service_.waitForFinished(1500);
  }
  engine_service_owned_ = false;
}

}  // namespace sar::gui
