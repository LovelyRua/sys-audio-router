#pragma once

#include "core/control/preset_document.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sar::gui_vstgui {

class PresetStore final {
 public:
  explicit PresetStore(std::filesystem::path directory = default_directory());

  [[nodiscard]] const std::filesystem::path& directory() const noexcept;
  [[nodiscard]] std::vector<std::string> names(std::string* error = nullptr) const;
  [[nodiscard]] bool save(std::string_view name,
                          const control::PresetDocument& preset,
                          std::string* error = nullptr) const;
  [[nodiscard]] bool load(std::string_view name,
                          control::PresetDocument* preset,
                          std::string* error = nullptr) const;
  [[nodiscard]] bool import_file(const std::filesystem::path& source,
                                 std::string_view name,
                                 std::string* error = nullptr) const;
  [[nodiscard]] bool export_file(std::string_view name,
                                 const std::filesystem::path& destination,
                                 std::string* error = nullptr) const;
  [[nodiscard]] bool rename(std::string_view current_name,
                            std::string_view new_name,
                            std::string* error = nullptr) const;
  [[nodiscard]] bool remove(std::string_view name,
                            std::string* error = nullptr) const;
  [[nodiscard]] bool contains(std::string_view name) const;

  [[nodiscard]] static bool valid_name(std::string_view name,
                                       std::string* error = nullptr);
  [[nodiscard]] static std::filesystem::path default_directory();

 private:
  [[nodiscard]] std::filesystem::path path_for_name(
      std::string_view name) const;

  std::filesystem::path directory_;
};

}  // namespace sar::gui_vstgui
