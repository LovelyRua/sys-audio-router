#pragma once

#include "core/control/preset_document.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include <utility>

namespace sar::gui_vstgui {

enum class RouteAction { Connect, Disconnect, Gain, Mute };
struct RouteCommand {
  RouteAction action;
  std::string input;
  std::string output;
  float gain = 1.0F;
  bool mute = false;
};

// UI-thread only. One serialized writer, with a bounded intent queue and a
// projected view so a late poll cannot undo an edit awaiting acknowledgement.
class RouteEditQueue {
 public:
  static constexpr std::size_t kCapacity = 64;

  bool enqueue(RouteCommand command) {
    if (command.input.empty() || command.output.empty() ||
        !std::isfinite(command.gain) || command.gain < 0.0F) return false;
    // Only adjacent continuous edits may collapse; never cross a mute or
    // disconnect boundary, or replace the command already sent to the engine.
    if (!queued_.empty() && command.action == RouteAction::Gain &&
        queued_.back().action == RouteAction::Gain &&
        sameRoute(queued_.back(), command)) {
      queued_.back() = std::move(command);
      return true;
    }
    if (queued_.size() + (active_ ? 1 : 0) >= kCapacity) return false;
    queued_.push_back(std::move(command));
    return true;
  }

  std::optional<RouteCommand> begin() {
    if (active_ || queued_.empty()) return std::nullopt;
    active_ = std::move(queued_.front());
    queued_.pop_front();
    return active_;
  }

  void complete(bool succeeded) {
    active_.reset();
    // Later edits can depend on the rejected operation (e.g. connect, gain).
    // Do not replay them against an unknown or disconnected engine state.
    if (!succeeded) queued_.clear();
  }

  void clear() { active_.reset(); queued_.clear(); }
  bool pending() const { return active_.has_value() || !queued_.empty(); }
  bool queued() const { return !queued_.empty(); }

  control::PresetRouteMatrix project(control::PresetRouteMatrix matrix) const {
    if (active_) apply(matrix, *active_);
    for (const auto& command : queued_) apply(matrix, command);
    return matrix;
  }

 private:
  static bool sameRoute(const RouteCommand& left, const RouteCommand& right) {
    return left.input == right.input && left.output == right.output;
  }

  static void apply(control::PresetRouteMatrix& matrix, const RouteCommand& command) {
    const auto hasInput = std::any_of(matrix.inputs.begin(), matrix.inputs.end(),
        [&](const auto& endpoint) { return endpoint.id == command.input; });
    const auto hasOutput = std::any_of(matrix.outputs.begin(), matrix.outputs.end(),
        [&](const auto& endpoint) { return endpoint.id == command.output; });
    if (!hasInput || !hasOutput) return;
    auto route = std::find_if(matrix.routes.begin(), matrix.routes.end(),
        [&](const auto& value) {
          return value.input_id == command.input && value.output_id == command.output;
        });
    if (command.action == RouteAction::Disconnect) {
      if (route != matrix.routes.end()) matrix.routes.erase(route);
    } else if (command.action == RouteAction::Connect) {
      if (route == matrix.routes.end())
        matrix.routes.push_back({command.input, command.output, command.gain, command.mute});
    } else if (route != matrix.routes.end()) {
      if (command.action == RouteAction::Gain) route->gain = command.gain;
      if (command.action == RouteAction::Mute) route->muted = command.mute;
    }
  }

  std::optional<RouteCommand> active_;
  std::deque<RouteCommand> queued_;
};

}  // namespace sar::gui_vstgui
