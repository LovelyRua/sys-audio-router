#include "app/gui_vstgui/preset_store.h"

#include "core/control/preset_file_codec.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <random>
#include <span>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace sar::gui_vstgui {
namespace {

constexpr std::string_view kFileSuffix = ".sarpreset";
constexpr std::uintmax_t kMaximumPresetFileBytes = 1024U * 1024U;

void set_error(std::string* error, std::string value) {
  if (error != nullptr) *error = std::move(value);
}

std::filesystem::path temporary_path_for(const std::filesystem::path& target);
bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  std::string* error);

std::string path_utf8(const std::filesystem::path& path) {
  const auto value = path.u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string filename_preset_name(const std::filesystem::path& path) {
  auto value = path.filename().stem().u8string();
  return {reinterpret_cast<const char*>(value.data()), value.size()};
}

bool ascii_iequal(std::string_view left, std::string_view right) {
  return left.size() == right.size() &&
         std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
           return std::tolower(static_cast<unsigned char>(a)) ==
                  std::tolower(static_cast<unsigned char>(b));
         });
}

bool read_preset_file(const std::filesystem::path& path,
                      control::PresetDocument* preset,
                      std::string* error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    set_error(error, "Could not read the preset file: " + ec.message());
    return false;
  }
  if (size > kMaximumPresetFileBytes) {
    set_error(error, "Preset file exceeds the 1 MiB safety limit");
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    set_error(error, "Could not open the preset file");
    return false;
  }
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!file || static_cast<std::size_t>(file.gcount()) != bytes.size()) {
    set_error(error, "Could not read the complete preset file");
    return false;
  }
  const auto decoded = control::decode_preset_file(bytes);
  if (!decoded.ok()) {
    set_error(error, decoded.error().message);
    return false;
  }
  *preset = decoded.preset();
  return true;
}

bool write_preset_file(const std::filesystem::path& destination,
                       const control::PresetDocument& preset,
                       std::string* error) {
  const auto encoded = control::encode_preset_file(preset);
  if (!encoded.ok()) {
    set_error(error, encoded.error().message);
    return false;
  }
  const auto temporary = temporary_path_for(destination);
  {
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
      set_error(error, "Could not open a temporary preset file");
      return false;
    }
    const auto& bytes = encoded.bytes();
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.flush();
    if (!file) {
      file.close();
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
      set_error(error, "Could not write the preset file");
      return false;
    }
  }
  if (replace_file(temporary, destination, error)) return true;
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  return false;
}

bool reserved_windows_name(std::string_view name) {
  const auto dot = name.find('.');
  auto stem = name.substr(0, dot);
  std::string lower(stem);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](char value) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  });
  if (lower == "con" || lower == "prn" || lower == "aux" || lower == "nul") {
    return true;
  }
  return lower.size() == 4 &&
         (lower.starts_with("com") || lower.starts_with("lpt")) &&
         lower[3] >= '1' && lower[3] <= '9';
}

std::filesystem::path temporary_path_for(const std::filesystem::path& target) {
  static std::random_device random;
  const auto nonce = (static_cast<std::uint64_t>(random()) << 32U) ^ random() ^
                     static_cast<std::uint64_t>(
                         std::chrono::steady_clock::now().time_since_epoch().count());
  return target.parent_path() /
         (target.filename().native() +
#ifdef _WIN32
          std::wstring(L".tmp-") + std::to_wstring(nonce));
#else
          std::string(".tmp-") + std::to_string(nonce));
#endif
}

bool replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& destination,
                  std::string* error) {
#ifdef _WIN32
  if (MoveFileExW(source.c_str(), destination.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    return true;
  }
  set_error(error, "Could not atomically replace the preset file (Windows " +
                        std::to_string(GetLastError()) + ")");
  return false;
#else
  std::error_code ec;
  std::filesystem::rename(source, destination, ec);
  if (!ec) return true;
  set_error(error, "Could not atomically replace the preset file: " +
                        ec.message());
  return false;
#endif
}

}  // namespace

PresetStore::PresetStore(std::filesystem::path directory)
    : directory_(std::move(directory)) {}

const std::filesystem::path& PresetStore::directory() const noexcept {
  return directory_;
}

std::vector<std::string> PresetStore::names(std::string* error) const {
  set_error(error, {});
  std::error_code ec;
  if (!std::filesystem::exists(directory_, ec)) {
    if (ec) set_error(error, "Could not inspect the preset directory: " + ec.message());
    return {};
  }
  if (!std::filesystem::is_directory(directory_, ec) || ec) {
    set_error(error, "The preset path is not a readable directory");
    return {};
  }

  std::vector<std::string> result;
  for (std::filesystem::directory_iterator it(directory_, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec) || ec) continue;
    const auto filename = path_utf8(it->path().filename());
    if (filename.size() <= kFileSuffix.size() ||
        filename.substr(filename.size() - kFileSuffix.size()) != kFileSuffix) {
      continue;
    }
    result.push_back(filename_preset_name(it->path()));
  }
  if (ec) {
    set_error(error, "Could not enumerate presets: " + ec.message());
    return {};
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    auto lower_left = left;
    auto lower_right = right;
    std::transform(lower_left.begin(), lower_left.end(), lower_left.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    std::transform(lower_right.begin(), lower_right.end(), lower_right.begin(),
                   [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return lower_left == lower_right ? left < right : lower_left < lower_right;
  });
  return result;
}

bool PresetStore::save(std::string_view name,
                       const control::PresetDocument& preset,
                       std::string* error) const {
  set_error(error, {});
  if (!valid_name(name, error)) return false;
  std::error_code ec;
  std::filesystem::create_directories(directory_, ec);
  if (ec) {
    set_error(error, "Could not create the preset directory: " + ec.message());
    return false;
  }

  const auto destination = path_for_name(name);
  return write_preset_file(destination, preset, error);
}

bool PresetStore::load(std::string_view name, control::PresetDocument* preset,
                       std::string* error) const {
  set_error(error, {});
  if (preset == nullptr) {
    set_error(error, "Preset destination is missing");
    return false;
  }
  if (!valid_name(name, error)) return false;
  return read_preset_file(path_for_name(name), preset, error);
}

bool PresetStore::import_file(const std::filesystem::path& source,
                              std::string_view name,
                              std::string* error) const {
  set_error(error, {});
  if (!valid_name(name, error)) return false;
  control::PresetDocument preset;
  if (!read_preset_file(source, &preset, error)) return false;
  return save(name, preset, error);
}

bool PresetStore::export_file(std::string_view name,
                              const std::filesystem::path& destination,
                              std::string* error) const {
  set_error(error, {});
  if (destination.empty()) {
    set_error(error, "Choose a destination for the preset export");
    return false;
  }
  control::PresetDocument preset;
  if (!load(name, &preset, error)) return false;
  return write_preset_file(destination, preset, error);
}

bool PresetStore::rename(std::string_view current_name,
                         std::string_view new_name,
                         std::string* error) const {
  set_error(error, {});
  if (!valid_name(current_name, error) || !valid_name(new_name, error)) return false;
  if (current_name == new_name) {
    if (contains(current_name)) return true;
    set_error(error, "The preset does not exist");
    return false;
  }
  if (ascii_iequal(current_name, new_name)) {
    set_error(error, "Preset names cannot differ only by letter case");
    return false;
  }
  if (contains(new_name)) {
    set_error(error, "A preset with that name already exists");
    return false;
  }
  control::PresetDocument preset;
  if (!load(current_name, &preset, error)) return false;
  if (!save(new_name, preset, error)) return false;
  if (remove(current_name, error)) return true;
  std::error_code ignored;
  std::filesystem::remove(path_for_name(new_name), ignored);
  return false;
}

bool PresetStore::remove(std::string_view name, std::string* error) const {
  set_error(error, {});
  if (!valid_name(name, error)) return false;
  std::error_code ec;
  if (!std::filesystem::remove(path_for_name(name), ec) || ec) {
    set_error(error, ec ? "Could not remove the preset: " + ec.message()
                        : "The preset does not exist");
    return false;
  }
  return true;
}

bool PresetStore::contains(std::string_view name) const {
  if (!valid_name(name)) return false;
  std::error_code ec;
  return std::filesystem::is_regular_file(path_for_name(name), ec) && !ec;
}

bool PresetStore::valid_name(std::string_view name, std::string* error) {
  set_error(error, {});
  if (name.empty() || name.size() > 80 || name == "." || name == ".." ||
      name.back() == '.' || name.back() == ' ' || reserved_windows_name(name)) {
    set_error(error, "Use a preset name up to 80 bytes without path characters");
    return false;
  }
  constexpr std::string_view forbidden = "<>:\"/\\|?*";
  for (const unsigned char character : name) {
    if (character < 0x20 || forbidden.find(static_cast<char>(character)) !=
                                 std::string_view::npos) {
      set_error(error, "Use a preset name up to 80 bytes without path characters");
      return false;
    }
  }
  return true;
}

std::filesystem::path PresetStore::default_directory() {
#ifdef _WIN32
  std::array<wchar_t, 32768> app_data{};
  const auto length = GetEnvironmentVariableW(L"APPDATA", app_data.data(),
                                               static_cast<DWORD>(app_data.size()));
  if (length > 0 && length < app_data.size()) {
    return std::filesystem::path(app_data.data()) / L"System Audio Route" /
           L"presets";
  }
#endif
  return std::filesystem::temp_directory_path() / "System Audio Route" /
         "presets";
}

std::filesystem::path PresetStore::path_for_name(std::string_view name) const {
  std::string filename(name);
  filename.append(kFileSuffix);
  return directory_ / std::filesystem::u8path(filename);
}

}  // namespace sar::gui_vstgui
