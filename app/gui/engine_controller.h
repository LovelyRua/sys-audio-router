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

#include <cstdint>
#include <deque>
#include <optional>

namespace sar::gui {

struct EngineReply {
  control::ControlCommandType request_type =
      control::ControlCommandType::QueryDiagnostics;
  control::ControlResponse response;
  QString error;
  bool transport_ok = false;
};

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
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int sampleRate READ sampleRate NOTIFY sessionChanged)
  Q_PROPERTY(int blockSize READ blockSize NOTIFY sessionChanged)
  Q_PROPERTY(qulonglong graphVersion READ graphVersion NOTIFY sessionChanged)
  Q_PROPERTY(qulonglong xrunCount READ xrunCount NOTIFY diagnosticsChanged)
  Q_PROPERTY(qulonglong droppedBlocks READ droppedBlocks NOTIFY diagnosticsChanged)
  Q_PROPERTY(int activeClients READ activeClients NOTIFY diagnosticsChanged)
  Q_PROPERTY(double peak READ peak NOTIFY diagnosticsChanged)
  Q_PROPERTY(double callbackPeakUs READ callbackPeakUs NOTIFY diagnosticsChanged)
  Q_PROPERTY(QVariantList inputs READ inputs NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList outputs READ outputs NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList routes READ routes NOTIFY sessionChanged)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY sessionChanged)
  Q_PROPERTY(int routeRevision READ routeRevision NOTIFY sessionChanged)
  Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
  Q_PROPERTY(QString activePresetName READ activePresetName NOTIFY presetsChanged)

 public:
  explicit EngineController(QObject* parent = nullptr);
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
  [[nodiscard]] bool busy() const noexcept;
  [[nodiscard]] int sampleRate() const noexcept;
  [[nodiscard]] int blockSize() const noexcept;
  [[nodiscard]] qulonglong graphVersion() const noexcept;
  [[nodiscard]] qulonglong xrunCount() const noexcept;
  [[nodiscard]] qulonglong droppedBlocks() const noexcept;
  [[nodiscard]] int activeClients() const noexcept;
  [[nodiscard]] double peak() const noexcept;
  [[nodiscard]] double callbackPeakUs() const noexcept;
  [[nodiscard]] QVariantList inputs() const;
  [[nodiscard]] QVariantList outputs() const;
  [[nodiscard]] QVariantList routes() const;
  [[nodiscard]] QVariantList devices() const;
  [[nodiscard]] int routeRevision() const noexcept;
  [[nodiscard]] QStringList presetNames() const;
  [[nodiscard]] QString activePresetName() const;

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
  Q_INVOKABLE bool routeEnabled(const QString& input_id,
                                const QString& output_id) const;
  Q_INVOKABLE double routeGain(const QString& input_id,
                               const QString& output_id) const;
  Q_INVOKABLE void setRoute(const QString& input_id,
                            const QString& output_id,
                            bool enabled);
  Q_INVOKABLE void setRouteGain(const QString& input_id,
                                const QString& output_id,
                                double gain);

 signals:
  void connectionChanged();
  void runtimeChanged();
  void busyChanged();
  void sessionChanged();
  void diagnosticsChanged();
  void feedbackChanged();
  void presetsChanged();

 private:
  enum class PendingPresetAction {
    None,
    Save,
    Load,
  };

  enum class RuntimeApplyStage {
    None,
    StopForReconfigure,
    ConfigureForReconfigure,
    RestartAfterReconfigure,
  };

  struct QueuedCommand {
    control::ControlCommand command;
    PendingPresetAction preset_action = PendingPresetAction::None;
    QString preset_name;
    RuntimeApplyStage runtime_stage = RuntimeApplyStage::None;
    bool poll = false;
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
  void schedulePoll();
  void ensureEngineService();
  void stopEngineService();
  void setError(QString error);
  void setStatus(QString status);

  QFutureWatcher<EngineReply> watcher_;
  QTimer poll_timer_;
  QProcess engine_service_;
  PresetStore preset_store_;
  std::deque<QueuedCommand> queued_commands_;
  std::optional<QueuedCommand> active_command_;
  std::optional<control::AudioRuntimeConfiguration>
      pending_runtime_reconfigure_;
  std::uint64_t command_sequence_ = 0;
  std::uint32_t poll_sequence_ = 0;
  bool connected_ = false;
  bool runtime_running_ = false;
  bool runtime_configured_ = false;
  control::AudioRuntimeMode runtime_mode_ = control::AudioRuntimeMode::None;
  QString runtime_capture_device_id_;
  QString runtime_render_device_id_;
  bool engine_service_owned_ = false;
  bool engine_service_start_attempted_ = false;
  bool shutting_down_ = false;
  bool busy_ = false;
  QString last_error_;
  QString status_message_;
  int sample_rate_ = 0;
  int block_size_ = 0;
  qulonglong graph_version_ = 0;
  qulonglong xrun_count_ = 0;
  qulonglong dropped_blocks_ = 0;
  int active_clients_ = 0;
  double peak_ = 0.0;
  double callback_peak_us_ = 0.0;
  QVariantList inputs_;
  QVariantList outputs_;
  QVariantList routes_;
  QVariantList devices_;
  int route_revision_ = 0;
  QStringList preset_names_;
  QString active_preset_name_;
};

}  // namespace sar::gui
