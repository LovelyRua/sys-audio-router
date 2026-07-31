#include "app/gui/preset_store.h"

#include "core/control/preset_file_codec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include <span>
#include <utility>

namespace sar::gui {
namespace {

constexpr auto kFileSuffix = ".sarpreset";

void set_error(QString* error, QString message) {
  if (error != nullptr) {
    *error = std::move(message);
  }
}

QString codec_error(const control::PresetFileError& error) {
  return QString::fromUtf8(error.message.data(),
                           static_cast<qsizetype>(error.message.size()));
}

}  // namespace

PresetStore::PresetStore(QString directory)
    : directory_(QDir::cleanPath(std::move(directory))) {}

const QString& PresetStore::directory() const noexcept { return directory_; }

QStringList PresetStore::names(QString* error) const {
  set_error(error, {});
  QDir directory(directory_);
  if (!directory.exists()) {
    return {};
  }
  if (!directory.isReadable()) {
    set_error(error, QStringLiteral("Preset directory is not readable"));
    return {};
  }

  const auto entries = directory.entryInfoList(
      {QStringLiteral("*") + QLatin1String(kFileSuffix)}, QDir::Files,
      QDir::Name | QDir::IgnoreCase);
  QStringList result;
  result.reserve(entries.size());
  for (const auto& entry : entries) {
    auto name = entry.fileName();
    name.chop(static_cast<qsizetype>(QLatin1String(kFileSuffix).size()));
    result.push_back(std::move(name));
  }
  return result;
}

bool PresetStore::save(const QString& name,
                       const control::PresetDocument& preset,
                       QString* error) const {
  set_error(error, {});
  if (!validName(name, error)) {
    return false;
  }
  const auto encoded = control::encode_preset_file(preset);
  if (!encoded.ok()) {
    set_error(error, codec_error(encoded.error()));
    return false;
  }
  if (!QDir().mkpath(directory_)) {
    set_error(error, QStringLiteral("Could not create the preset directory"));
    return false;
  }

  QSaveFile file(pathForName(name));
  if (!file.open(QIODevice::WriteOnly)) {
    set_error(error, file.errorString());
    return false;
  }
  const auto& bytes = encoded.bytes();
  const auto written =
      file.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<qsizetype>(bytes.size()));
  if (written != static_cast<qint64>(bytes.size()) || !file.commit()) {
    set_error(error, file.errorString());
    return false;
  }
  return true;
}

bool PresetStore::load(const QString& name,
                       control::PresetDocument* preset,
                       QString* error) const {
  set_error(error, {});
  if (preset == nullptr) {
    set_error(error, QStringLiteral("Preset destination is missing"));
    return false;
  }
  if (!validName(name, error)) {
    return false;
  }

  QFile file(pathForName(name));
  if (!file.open(QIODevice::ReadOnly)) {
    set_error(error, file.errorString());
    return false;
  }
  const auto bytes = file.readAll();
  const auto decoded = control::decode_preset_file(std::span{
      reinterpret_cast<const std::uint8_t*>(bytes.constData()),
      static_cast<std::size_t>(bytes.size()),
  });
  if (!decoded.ok()) {
    set_error(error, codec_error(decoded.error()));
    return false;
  }
  *preset = decoded.preset();
  return true;
}

bool PresetStore::validName(const QString& name, QString* error) {
  set_error(error, {});
  static const QRegularExpression invalid_characters(
      QStringLiteral(R"([<>:"/\\|?*\x00-\x1f])"));
  static const QRegularExpression reserved_name(
      QStringLiteral(R"(^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?$)"),
      QRegularExpression::CaseInsensitiveOption);

  if (name.isEmpty() || name.size() > 80 || name != name.trimmed() ||
      name == QStringLiteral(".") || name == QStringLiteral("..") ||
      name.endsWith(QLatin1Char('.')) ||
      invalid_characters.match(name).hasMatch() ||
      reserved_name.match(name).hasMatch()) {
    set_error(error,
              QStringLiteral("Use a short preset name without path characters"));
    return false;
  }
  return true;
}

QString PresetStore::pathForName(const QString& name) const {
  return QDir(directory_).filePath(name + QLatin1String(kFileSuffix));
}

}  // namespace sar::gui
