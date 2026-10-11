#include "app/gui_vstgui/preset_store.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

sar::control::PresetDocument make_preset(float gain, bool muted) {
  sar::control::PresetDocument preset;
  preset.sample_rate = 96000;
  preset.frames_per_block = 64;
  preset.matrix.inputs = {{"capture-1", "Interface input"}};
  preset.matrix.outputs = {{"render-1", "Studio output"}};
  preset.matrix.routes = {{"capture-1", "render-1", gain, muted}};
  return preset;
}

}  // namespace

int main() {
  using namespace sar;
  const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path() /
                         ("sar-vstgui-presets-" + std::to_string(nonce));
  const auto exchange_directory = directory / "exchange";
  std::filesystem::create_directories(exchange_directory);
  gui_vstgui::PresetStore store(directory);
  std::string error;

  assert(gui_vstgui::PresetStore::valid_name("Studio A", &error));
  assert(error.empty());
  for (const auto invalid : {"", ".", "..", "../outside", "C:\\route",
                             "bad/name", "bad?name", "trailing.", "CON",
                             "Lpt2.txt"}) {
    assert(!gui_vstgui::PresetStore::valid_name(invalid, &error));
    assert(!error.empty());
  }
  assert(!gui_vstgui::PresetStore::valid_name(std::string(81, 'x')));

  assert(store.names(&error).empty());
  assert(error.empty());
  assert(store.save("Studio B", make_preset(0.5F, false), &error));
  assert(store.save("Studio A", make_preset(0.25F, true), &error));
  assert(error.empty());
  assert(store.contains("Studio A"));
  const auto listed = store.names(&error);
  assert((listed == std::vector<std::string>{"Studio A", "Studio B"}));

  control::PresetDocument loaded;
  assert(store.load("Studio A", &loaded, &error));
  assert(loaded.sample_rate == 96000);
  assert(loaded.frames_per_block == 64);
  assert(loaded.matrix.inputs.front().id == "capture-1");
  assert(loaded.matrix.outputs.front().label == "Studio output");
  assert(loaded.matrix.routes.size() == 1);
  assert(loaded.matrix.routes.front().gain == 0.25F);
  assert(loaded.matrix.routes.front().muted);

  assert(store.save("Studio A", make_preset(0.75F, false), &error));
  assert(store.load("Studio A", &loaded, &error));
  assert(loaded.matrix.routes.front().gain == 0.75F);

  const auto exported = exchange_directory / "Studio B.sarpreset";
  assert(store.export_file("Studio B", exported, &error));
  assert(error.empty());
  assert(store.import_file(exported, "Imported", &error));
  assert(store.load("Imported", &loaded, &error));
  assert(loaded.matrix.routes.front().gain == 0.5F);
  assert(!store.import_file(exported, "../escape", &error));
  assert(!error.empty());
  assert(!store.export_file("missing", exchange_directory / "missing.sarpreset",
                            &error));
  assert(!error.empty());
  assert(!store.export_file("Studio B", {}, &error));
  assert(!error.empty());

  assert(store.rename("Imported", "Studio C", &error));
  assert(!store.contains("Imported"));
  assert(store.contains("Studio C"));
  assert(!store.rename("Studio C", "Studio B", &error));
  assert(!error.empty());
  assert(!store.rename("Studio C", "studio c", &error));
  assert(!error.empty());
  assert(store.rename("Studio C", "Studio C", &error));
  assert(!store.rename("missing", "Another", &error));
  assert(!error.empty());
  assert(store.remove("Studio C", &error));
  assert(!loaded.matrix.routes.front().muted);

  auto invalid_preset = make_preset(0.0F, false);
  invalid_preset.sample_rate = 0;
  assert(!store.save("Studio A", invalid_preset, &error));
  assert(!error.empty());
  assert(store.load("Studio A", &loaded, &error));
  assert(loaded.matrix.routes.front().gain == 0.75F);

  assert(!store.load("missing", &loaded, &error));
  assert(!error.empty());
  assert(!store.load("../outside", &loaded, &error));
  assert(!store.load("Studio B", nullptr, &error));
  assert(!store.remove("missing", &error));

  {
    std::ofstream broken(directory / "Broken.sarpreset", std::ios::binary);
    broken << "not a SAR preset";
  }
  assert(!store.load("Broken", &loaded, &error));
  assert(!error.empty());
  assert(!store.import_file(directory / "Broken.sarpreset", "Studio B", &error));
  assert(!error.empty());
  assert(store.load("Studio B", &loaded, &error));
  assert(loaded.matrix.routes.front().gain == 0.5F);
  assert(!store.import_file(directory / "Broken.sarpreset", "Broken copy", &error));
  assert(!error.empty());
  {
    std::ofstream oversized(directory / "Oversized.sarpreset", std::ios::binary);
    oversized << std::string(1024U * 1024U + 1U, 'x');
  }
  assert(!store.import_file(directory / "Oversized.sarpreset", "Too large", &error));
  assert(error.find("1 MiB") != std::string::npos);
  assert(store.remove("Broken", &error));
  assert(!store.contains("Broken"));
  assert(store.remove("Studio A", &error));
  assert(!store.contains("Studio A"));
  assert((store.names(&error) == std::vector<std::string>{"Studio B"}));

  std::error_code ignored;
  std::filesystem::remove_all(directory, ignored);
  return 0;
}
