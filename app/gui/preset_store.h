#pragma once

#include "core/control/preset_document.h"

#include <QString>
#include <QStringList>

namespace sar::gui {

class PresetStore final {
 public:
  explicit PresetStore(QString directory);

  [[nodiscard]] const QString& directory() const noexcept;
  [[nodiscard]] QStringList names(QString* error = nullptr) const;
  [[nodiscard]] bool save(const QString& name,
                          const control::PresetDocument& preset,
                          QString* error = nullptr) const;
  [[nodiscard]] bool load(const QString& name,
                          control::PresetDocument* preset,
                          QString* error = nullptr) const;

  [[nodiscard]] static bool validName(const QString& name,
                                      QString* error = nullptr);

 private:
  [[nodiscard]] QString pathForName(const QString& name) const;

  QString directory_;
};

}  // namespace sar::gui
