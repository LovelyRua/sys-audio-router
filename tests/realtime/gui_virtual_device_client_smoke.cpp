#include "app/gui_vstgui/virtual_device_client.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
using namespace sar::gui_vstgui;

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
    std::exit(1);
  }
}

std::string instance(const std::string& id = R"(ROOT\\MEDIA\\0001)",
                     const std::string& label = "Test adapter") {
  return "{\"id\":\"" + id + "\",\"label\":\"" + label +
      "\",\"started\":true,\"problem\":false,\"problemCode\":0}";
}

std::string roster(const std::string& items) {
  return "{\"maximum\":4,\"instances\":[" + items + "]}";
}

void rejected(const std::string& json) {
  const auto result = parseVirtualDeviceSnapshot(json);
  require(!result.available && result.maximum == 0 && result.instances.empty() &&
      !result.error.empty(), "Parser must fail closed without partial inventory");
}

void replaceAndReject(std::string json, const std::string& before, const std::string& after) {
  const auto offset = json.find(before);
  require(offset != std::string::npos, "Invalid test replacement");
  json.replace(offset, before.size(), after);
  rejected(json);
}

void invalidAction(VirtualDeviceAction action, const std::string& id,
                   const std::string& label, const std::string& inf = {}) {
  // All inputs here fail local validation before helper lookup or a UAC prompt.
  const auto result = VirtualDeviceClient{}.execute(action, id, label, inf);
  require(!result.success && !result.cancelled && !result.outcomeUnknown &&
      !result.message.empty() && !result.snapshot.available,
      "Invalid request must be rejected before launching anything");
}
}  // namespace

int main() {
  using namespace sar::gui_vstgui;
  const auto empty = parseVirtualDeviceSnapshot(roster(""));
  require(empty.available && empty.maximum == 4 && empty.instances.empty() &&
      empty.error.empty(), "Empty inventory is a valid installed helper response");
  const auto good = roster(instance());
  const auto parsed = parseVirtualDeviceSnapshot(good);
  require(parsed.available && parsed.instances.size() == 1, "Valid inventory rejected");
  const auto& first = parsed.instances.front();
  require(first.id == "ROOT\\MEDIA\\0001" && first.label == "Test adapter" &&
      first.started && !first.problem && first.problemCode == 0,
      "PnP status fields must be preserved without inferring audio readiness");
  require(parseVirtualDeviceSnapshot("\xEF\xBB\xBF" + good).available,
      "Optional UTF-8 BOM must be accepted");
  require(parseVirtualDeviceSnapshot(" \r\n" + good + "\r\n").available,
      "JSON whitespace must be accepted");
  const std::string chinese = "\xE8\x99\x9A\xE6\x8B\x9F\xE9\x9F\xB3\xE9\xA2\x91";
  require(parseVirtualDeviceSnapshot(roster(instance(R"(root\\media\\12)", chinese)))
      .instances.front().label == chinese, "UTF-8 labels must round trip");
  require(parseVirtualDeviceSnapshot(roster(instance(R"(ROOT\\MEDIA\\2)",
      R"(\u865a\u62df\u97f3\u9891)"))).instances.front().label == chinese,
      "JSON Unicode escapes must decode to UTF-8");
  require(parseVirtualDeviceSnapshot(roster(instance(R"(ROOT\\MEDIA\\2)",
      R"(A \"quoted\" path\\)"))).available, "Escaped label rejected");
  require(parseVirtualDeviceSnapshot(roster(instance(R"(ROOT\\MEDIA\\2)",
      std::string(80, 'x')))).available, "80-unit label rejected");
  auto maximumProblem = good;
  maximumProblem.replace(maximumProblem.find("\"problemCode\":0"), 15,
      "\"problemCode\":4294967295");
  const auto maxParsed = parseVirtualDeviceSnapshot(maximumProblem);
  require(maxParsed.available && maxParsed.instances.front().problemCode == 4294967295UL,
      "Full DWORD problem code must round trip");
  std::string four;
  for (int i = 1; i <= 4; ++i) {
    if (!four.empty()) four += ',';
    four += instance(R"(ROOT\\MEDIA\\)" + std::to_string(i));
  }
  require(parseVirtualDeviceSnapshot(roster(four)).instances.size() == 4,
      "Four adapters must be accepted");
  rejected(roster(four + ',' + instance(R"(ROOT\\MEDIA\\5)")));
  rejected(roster(instance() + ',' + instance(R"(root\\media\\0001)")));
  rejected("");
  rejected("{}");
  rejected("[]");
  rejected("null");
  rejected("{broken");
  rejected(std::string(1024 * 1024 + 1, ' '));
  rejected(roster(instance(R"(ROOT\\MEDIA\\1)", "bad\xFF")));
  rejected(roster(instance(R"(ROOT\\MEDIA\\1)", R"(\uD800)")));
  rejected(roster(instance(R"(ROOT\\MEDIA\\1)", "")));
  rejected(roster(instance(R"(ROOT\\MEDIA\\1)", "   ")));
  rejected(roster(instance(R"(ROOT\\MEDIA\\1)", std::string(81, 'a'))));
  for (const auto* control : {R"(\u0000)", R"(\n)", R"(\u007f)", R"(\u0085)"})
    rejected(roster(instance(R"(ROOT\\MEDIA\\1)", std::string("bad") + control)));
  for (const auto* id : {"", R"(ROOT\\OTHER\\1)", R"(ROOT\\MEDIA\\)",
                        R"(ROOT\\MEDIA\\1x)", R"(ROOT\\MEDIA\\1\u0000)",
                        R"(ROOT\\MEDIA\\1\n)", R"(ROOT\\MEDIA\\-1)"})
    rejected(roster(instance(id)));
  rejected(roster(instance(R"(ROOT\\MEDIA\\)" + std::string(190, '1'))));
  for (const auto* maximum : {"3", "5", "-1", "4.5", "\"4\"", "true", "null"})
    replaceAndReject(good, "\"maximum\":4", std::string("\"maximum\":") + maximum);
  for (const auto* code : {"-1", "0.5", "4294967296", "1e100", "\"0\"", "null", "false"})
    replaceAndReject(good, "\"problemCode\":0", std::string("\"problemCode\":") + code);
  replaceAndReject(good, "\"started\":true", "\"started\":1");
  replaceAndReject(good, "\"problem\":false", "\"problem\":\"false\"");
  replaceAndReject(good, "\"label\":\"Test adapter\"", "\"label\":null");
  replaceAndReject(good, "\"id\":\"ROOT\\\\MEDIA\\\\0001\"", "\"id\":7");
  replaceAndReject(good, "\"problemCode\":0", "\"other\":0");
  rejected("{\"maximum\":4,\"instances\":{}}");
  rejected(roster("null"));
  rejected(roster(instance() + ",null"));

  const std::string id = "ROOT\\MEDIA\\1";
  invalidAction(static_cast<VirtualDeviceAction>(999), {}, {});
  for (const auto* badId : {"", "ROOT\\OTHER\\1", "ROOT\\MEDIA\\", "ROOT\\MEDIA\\1 --confirm",
                           "ROOT\\MEDIA\\1\n", "ROOT\\MEDIA\\-1"}) {
    invalidAction(VirtualDeviceAction::Remove, badId, {});
    invalidAction(VirtualDeviceAction::Rename, badId, "Label");
  }
  invalidAction(VirtualDeviceAction::Remove, std::string(201, 'x'), {});
  invalidAction(VirtualDeviceAction::Remove, id + std::string(1, '\0'), {});
  for (const auto& label : {std::string{}, std::string("  "), std::string(81, 'x'),
                           std::string("bad\nlabel"), std::string("bad\xFF"),
                           std::string("bad\xC2\x85"), std::string("bad\0label", 9)}) {
    invalidAction(VirtualDeviceAction::Rename, id, label);
    invalidAction(VirtualDeviceAction::Add, {}, label, "relative.inf");
  }
  for (const auto* inf : {"", "relative.inf", "C:relative.inf", "\\rooted.inf",
                         "C:\\missing-driver.txt", "\\\\.\\device.inf", "\\\\?\\C:\\file.inf"})
    invalidAction(VirtualDeviceAction::Add, {}, "Label", inf);
  invalidAction(VirtualDeviceAction::Add, {}, "Label", std::string("C:\\bad\0.inf", 11));
  invalidAction(VirtualDeviceAction::Add, {}, "Label", "C:\\bad\n.inf");
  invalidAction(VirtualDeviceAction::Add, {}, "Label", "C:\\bad\xFF.inf");
  // Deliberately no valid execute() calls: this test must never request elevation or mutate a driver.
}
