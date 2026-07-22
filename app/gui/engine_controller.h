#pragma once

#include "core/control/control_command.h"
#include "core/control/control_response.h"

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <cstdint>

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
  Q_PROPERTY(QString lastError READ lastError NOTIFY connectionChanged)
  Q_PROPERTY(bool runtimeRunning READ runtimeRunning NOTIFY runtimeChanged)
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

 public:
  explicit EngineController(QObject* parent = nullptr);

  [[nodiscard]] bool connected() const noexcept;
  [[nodiscard]] QString connectionLabel() const;
  [[nodiscard]] QString lastError() const;
  [[nodiscard]] bool runtimeRunning() const noexcept;
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

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void startRuntime();
  Q_INVOKABLE void stopRuntime();
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

 private:
  void dispatch(control::ControlCommand command);
  void applyReply(const EngineReply& reply);
  void updateSession(const control::ControlResponse& response);
  void schedulePoll();

  QFutureWatcher<EngineReply> watcher_;
  QTimer poll_timer_;
  std::uint64_t command_sequence_ = 0;
  std::uint32_t poll_sequence_ = 0;
  bool connected_ = false;
  bool runtime_running_ = false;
  bool busy_ = false;
  QString last_error_;
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
};

}  // namespace sar::gui
