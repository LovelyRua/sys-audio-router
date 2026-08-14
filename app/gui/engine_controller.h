#pragma once

#include "core/control/control_command.h"
#include "core/control/control_response.h"
#include "app/gui/preset_store.h"

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace sar::gui {

struct EngineReply {
  control::ControlCommandType request_type =
      control::ControlCommandType::QueryDiagnostics;
  control::ControlResponse response;
  QString error;
  bool transport_ok = false;
  bool delivery_uncertain = false;
};

using EngineTransport =
    std::function<EngineReply(control::ControlCommand command)>;

class EngineController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
  Q_PROPERTY(QString connectionLabel READ connectionLabel NOTIFY connectionChanged)
  Q_PROPERTY(QString lastError READ lastError NOTIFY feedbackChanged)
  Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY feedbackChanged)
  Q_PROPERTY(bool runtimeRunning READ runtimeRunning NOTIFY runtimeChanged)
  Q_PROPERTY(bool runtimeConfigured READ runtimeConfigured NOTIFY runtimeChanged)
  Q_PROPERTY(QString runtimeMode READ runtimeMode NOTIFY runtimeChanged)
  Q_PROPERTY(QString runtimeCaptureDeviceId READ runtimeCaptureDeviceId NOTIFY runtimeChanged)
  Q_PROPERTY(QString runtimeRenderDeviceId READ runtimeRenderDeviceId NOTIFY runtimeChanged)
  Q_PROPERTY(QVariantList runtimeEndpoints READ runtimeEndpoints NOTIFY runtimeChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int sampleRate READ sampleRate NOTIFY sessionChanged)
  Q_PROPERTY(int blockSize READ blockSize NOTIFY sessionChanged)
  Q_PROPERTY(qulonglong graphVersion READ graphVersion NOTIFY sessionChanged)
  Q_PROPERTY(qulonglong xrunCount READ xrunCount NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong virtualAsioProducerUnderflows READ virtualAsioProducerUnderflows NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong virtualAsioProducerOverflows READ virtualAsioProducerOverflows NOTIFY diagnosticsChanged)
  Q_PROPERTY(int activeClients READ activeClients NOTIFY diagnosticsChanged)
  Q_PROPERTY(double peak READ peak NOTIFY diagnosticsChanged)
  Q_PROPERTY(double callbackPeakUs READ callbackPeakUs NOTIFY diagnosticsChanged)
  Q_PROPERTY(QVariantList endpointDiagnostics READ endpointDiagnostics NOTIFY diagnosticsChanged)
  Q_PROPERTY(bool wasapiRecoveryAvailable READ wasapiRecoveryAvailable NOTIFY diagnosticsChanged)
  Q_PROPERTY(QString wasapiRecoveryState READ wasapiRecoveryState NOTIFY diagnosticsChanged)
  Q_PROPERTY(QString wasapiRuntimeHealth READ wasapiRuntimeHealth NOTIFY diagnosticsChanged)
  Q_PROPERTY(QString wasapiRuntimeReasonCode READ wasapiRuntimeReasonCode NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiRecoveryEpisodes READ wasapiRecoveryEpisodes NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiSuccessfulRecoveries READ wasapiSuccessfulRecoveries NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiFailedRecoveries READ wasapiFailedRecoveries NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiLastRecoveryMs READ wasapiLastRecoveryMs NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiMaximumRecoveryMs READ wasapiMaximumRecoveryMs NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiEndpointReopens READ wasapiEndpointReopens NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiEndpointResetFailures READ wasapiEndpointResetFailures NOTIFY diagnosticsChanged)
  Q_PROPERTY(bool wasapiEndpointReopenPending READ wasapiEndpointReopenPending NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiWaitTimeoutCycles READ wasapiWaitTimeoutCycles NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiCaptureDiscontinuityCycles READ wasapiCaptureDiscontinuityCycles NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiRenderFifoUnderflowFrames READ wasapiRenderFifoUnderflowFrames NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiMaximumRenderRecoverySilenceFrames READ wasapiMaximumRenderRecoverySilenceFrames NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong wasapiMaximumConsecutiveCaptureRateClampedFrames READ wasapiMaximumConsecutiveCaptureRateClampedFrames NOTIFY diagnosticsChanged)
  Q_PROPERTY(QVariantList inputs READ inputs NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList outputs READ outputs NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList routes READ routes NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList virtualAsioDevices READ virtualAsioDevices NOTIFY virtualAsioDevicesChanged)
  Q_PROPERTY(int routeRevision READ routeRevision NOTIFY sessionChanged)
  Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
  Q_PROPERTY(QString activePresetName READ activePresetName NOTIFY presetsChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

 public:
  explicit EngineController(QObject* parent = nullptr);
  EngineController(EngineTransport transport,
                   bool automatic_activity,
                   QObject* parent = nullptr);
  ~EngineController() override;

  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] QString connectionLabel() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] QString statusMessage() const;
  [[nodiscard]] bool runtimeRunning() const noexcept;
  [[nodiscard]] bool runtimeConfigured() const noexcept;
  [[nodiscard]] QString runtimeMode() const;
  [[nodiscard]] QString runtimeCaptureDeviceId() const;
  [[nodiscard]] QString runtimeRenderDeviceId() const;
  [[nodiscard]] QVariantList runtimeEndpoints() const;
  [[nodiscard]] bool busy() const noexcept;
  [[nodiscard]] int sampleRate() const noexcept;
  [[nodiscard]] int blockSize() const noexcept;
  [[nodiscard]] qulonglong graphVersion() const noexcept;
  [[nodiscard]] qulonglong xrunCount() const noexcept;
  [[nodiscard]] qulonglong droppedBlocks() const noexcept;
  [[nodiscard]] qulonglong virtualAsioProducerUnderflows() const noexcept;
  [[nodiscard]] qulonglong virtualAsioProducerOverflows() const noexcept;
  [[nodiscard]] int activeClients() const noexcept;
  [[nodiscard]] double peak() const noexcept;
  [[nodiscard]] double callbackPeakUs() const noexcept;
  [[nodiscard]] QVariantList endpointDiagnostics() const;
  [[nodiscard]] bool wasapiRecoveryAvailable() const noexcept;
  [[nodiscard]] QString wasapiRecoveryState() const;
  [[nodiscard]] QString wasapiRuntimeHealth() const;
  [[nodiscard]] QString wasapiRuntimeReasonCode() const;
  [[nodiscard]] qulonglong wasapiRecoveryEpisodes() const noexcept;
  [[nodiscard]] qulonglong wasapiSuccessfulRecoveries() const noexcept;
  [[nodiscard]] qulonglong wasapiFailedRecoveries() const noexcept;
  [[nodiscard]] qulonglong wasapiLastRecoveryMs() const noexcept;
  [[nodiscard]] qulonglong wasapiMaximumRecoveryMs() const noexcept;
  [[nodiscard]] qulonglong wasapiEndpointReopens() const noexcept;
  [[nodiscard]] qulonglong wasapiEndpointResetFailures() const noexcept;
  [[nodiscard]] bool wasapiEndpointReopenPending() const noexcept;
  [[nodiscard]] qulonglong wasapiWaitTimeoutCycles() const noexcept;
  [[nodiscard]] qulonglong wasapiCaptureDiscontinuityCycles() const noexcept;
  [[nodiscard]] qulonglong wasapiRenderFifoUnderflowFrames() const noexcept;
  [[nodiscard]] qulonglong wasapiMaximumRenderRecoverySilenceFrames() const noexcept;
  [[nodiscard]] qulonglong
  wasapiMaximumConsecutiveCaptureRateClampedFrames() const noexcept;
  [[nodiscard]] QVariantList inputs() const;
  [[nodiscard]] QVariantList outputs() const;
  [[nodiscard]] QVariantList routes() const;
  [[nodiscard]] QVariantList devices() const;
  [[nodiscard]] QVariantList virtualAsioDevices() const;
  [[nodiscard]] int routeRevision() const noexcept;
  [[nodiscard]] QStringList presetNames() const;
  [[nodiscard]] QString activePresetName() const;
  [[nodiscard]] bool canUndo() const noexcept;
  [[nodiscard]] bool canRedo() const noexcept;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void refreshPresets();
  Q_INVOKABLE void savePreset(const QString& name);
  Q_INVOKABLE void loadPreset(const QString& name);
  Q_INVOKABLE void clearFeedback();
  Q_INVOKABLE void startRuntime();
  Q_INVOKABLE void stopRuntime();
  Q_INVOKABLE void configureAudioRuntime(const QString& mode,
                                         const QString& capture_device_id,
                                         const QString& render_device_id);
  Q_INVOKABLE void configureAudioMatrix(const QVariantList& endpoints);
  Q_INVOKABLE void configureVirtualAsioDevices(const QVariantList& devices);
  Q_INVOKABLE bool routeEnabled(const QString& input_id,
                                const QString& output_id) const;
  Q_INVOKABLE double routeGain(const QString& input_id,
                               const QString& output_id) const;
  Q_INVOKABLE void setRoute(const QString& input_id,
                             const QString& output_id,
                             bool enabled);
  Q_INVOKABLE void removeRoute(const QString& input_id,
                               const QString& output_id);
  Q_INVOKABLE void setRouteGain(const QString& input_id,
                                const QString& output_id,
                                double gain);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

 signals:
  void connectionChanged();
  void runtimeChanged();
  void busyChanged();
  void sessionChanged();
  void virtualAsioDevicesChanged();
  void virtualAsioDevicesRefreshed();
  void virtualAsioTopologyApplied();
  void diagnosticsChanged();
  void feedbackChanged();
  void presetsChanged();
  void historyChanged();

 private:
  enum class PendingPresetAction {
    None,
    Save,
    Load,
  };

  enum class HistoryAction {
    None,
    Record,
    Undo,
    Redo,
    Reset,
  };

  struct HistoryEntry {
    control::PresetDocument before;
    control::PresetDocument after;
  };

  struct QueuedCommand {
    control::ControlCommand command;
    PendingPresetAction preset_action = PendingPresetAction::None;
    QString preset_name;
    bool poll = false;
    HistoryAction history_action = HistoryAction::None;
    std::optional<HistoryEntry> history_entry;
  };

  void dispatch(control::ControlCommand command);
  void dispatchPreset(control::ControlCommand command,
                      PendingPresetAction action,
                      QString name);
  void enqueue(QueuedCommand command);
  void startNextCommand();
  void updateBusyState();
  void applyReply(const EngineReply& reply, const QueuedCommand& command);
  void updateSession(const control::ControlResponse& response);
  void updateVirtualAsioDevices(const control::ControlResponse& response);
  void updatePresetView(const control::PresetDocument& preset);
  Q_SLOT void schedulePoll();
  void ensureEngineService();
  void stopEngineService();
  void setError(QString error);
  void setStatus(QString status);
  void dispatchHistoryLoad(const HistoryEntry& entry, HistoryAction action);
  void commitHistory(const QueuedCommand& command);
  void pushBounded(std::deque<HistoryEntry>& history, HistoryEntry entry);

  QFutureWatcher<EngineReply> watcher_;
  EngineTransport transport_;
  QTimer poll_timer_;
  QProcess engine_service_;
  PresetStore preset_store_;
  std::deque<QueuedCommand> queued_commands_;
  std::optional<QueuedCommand> active_command_;
  std::string command_prefix_;
  std::uint64_t command_sequence_ = 0;
  std::uint32_t poll_sequence_ = 0;
  bool connected_ = false;
  bool runtime_running_ = false;
  bool runtime_configured_ = false;
  control::AudioRuntimeMode runtime_mode_ = control::AudioRuntimeMode::None;
  QString runtime_capture_device_id_;
  QString runtime_render_device_id_;
  QVariantList runtime_endpoints_;
  bool engine_service_owned_ = false;
  bool engine_service_start_attempted_ = false;
  bool virtual_asio_restart_armed_ = false;
  bool service_management_enabled_ = true;
  bool connection_error_active_ = false;
  bool shutting_down_ = false;
  bool busy_ = false;
  QString last_error_;
  QString status_message_;
  int sample_rate_ = 0;
  int block_size_ = 0;
  qulonglong graph_version_ = 0;
  qulonglong xrun_count_ = 0;
  qulonglong dropped_blocks_ = 0;
  qulonglong virtual_asio_producer_underflows_ = 0;
  qulonglong virtual_asio_producer_overflows_ = 0;
  int active_clients_ = 0;
  double peak_ = 0.0;
  double callback_peak_us_ = 0.0;
  QVariantList endpoint_diagnostics_;
  bool wasapi_recovery_available_ = false;
  control::WasapiRecoveryDiagnostics wasapi_recovery_;
  QVariantList inputs_;
  QVariantList outputs_;
  QVariantList routes_;
  QVariantList devices_;
  QVariantList virtual_asio_devices_;
  int route_revision_ = 0;
  QStringList preset_names_;
  QString active_preset_name_;
  std::optional<control::PresetDocument> current_preset_;
  std::deque<HistoryEntry> undo_history_;
  std::deque<HistoryEntry> redo_history_;
  static constexpr std::size_t kHistoryLimit = 64;
};

}  // namespace sar::gui
