#pragma once

#include "core/control/control_command.h"
#include "core/control/control_response.h"
#include "core/diagnostics/engine_diagnostics.h"
#include "core/graph/graph.h"
#include "core/graph/graph_snapshot.h"
#include "core/platform/virtual_endpoint.h"
#include "core/realtime/audio_buffer.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace sar::control {

class ControlSessionCreateResult;

class ControlSession {
 public:
  static ControlSessionCreateResult create(PresetDocument initial_preset,
                                           std::uint64_t initial_graph_version = 1);

  ControlResponse handle(const ControlCommand& command);
  ControlResponse handle(const ControlCommand& command,
                         diagnostics::EngineDiagnostics diagnostics);
  ControlResponse handle_batch(std::string command_id,
                               const std::vector<ControlCommand>& commands);

  void process(const realtime::AudioBuffer& input,
               realtime::AudioBuffer& output,
               diagnostics::EngineDiagnostics& diagnostics) noexcept;

  [[nodiscard]] const PresetDocument& current_preset() const noexcept;
  [[nodiscard]] std::shared_ptr<graph::Graph> current_graph() const noexcept;
  [[nodiscard]] const std::vector<platform::VirtualEndpointDescriptor>&
  virtual_endpoints() const noexcept;
  [[nodiscard]] std::uint64_t next_graph_version() const noexcept;

 private:
  ControlSession(PresetDocument preset,
                 std::shared_ptr<graph::Graph> graph,
                 std::uint64_t next_graph_version) noexcept;

  [[nodiscard]] ControlResponse publish_preset(const ControlCommand& command,
                                               PresetDocument preset);

  PresetDocument current_preset_;
  graph::GraphSnapshotPublisher publisher_;
  platform::VirtualEndpointRegistry virtual_endpoints_;
  std::uint64_t next_graph_version_;
};

class ControlSessionCreateResult {
 public:
  static ControlSessionCreateResult success(std::unique_ptr<ControlSession> session);
  static ControlSessionCreateResult failure(std::vector<PresetError> errors);

  [[nodiscard]] bool ok() const noexcept;
  [[nodiscard]] ControlSession& session() noexcept;
  [[nodiscard]] const ControlSession& session() const noexcept;
  [[nodiscard]] std::unique_ptr<ControlSession> take_session() noexcept;
  [[nodiscard]] const std::vector<PresetError>& errors() const noexcept;

 private:
  ControlSessionCreateResult(std::unique_ptr<ControlSession> session,
                             std::vector<PresetError> errors);

  std::unique_ptr<ControlSession> session_;
  std::vector<PresetError> errors_;
};

}  // namespace sar::control
