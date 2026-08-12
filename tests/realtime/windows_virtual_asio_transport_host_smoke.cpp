#include "core/graph/graph.h"
#include "core/platform/windows_virtual_asio_shared_memory.h"
#include "core/service/windows_virtual_asio_transport_host.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <cassert>
#include <memory>
#include <string>

namespace {
std::unique_ptr<sar::graph::Graph> make_graph(std::uint64_t version,
                                              std::uint32_t channels = 2) {
  auto graph = std::make_unique<sar::graph::Graph>(version, channels, 32, 48000);
  graph->add_node(std::make_unique<sar::graph::PassthroughNode>());
  return graph;
}

sar::service::WindowsVirtualAsioHostConnectRequest request(
    std::string id, std::uint32_t rate = 48000) {
  return {.client = {.client_id = std::move(id),
                     .process_id = GetCurrentProcessId(),
                     .format = {rate, 32, 2, 2}},
          .client_nonce_low = 101,
          .client_nonce_high = 202};
}
}  // namespace

int main() {
  sar::platform::VirtualAsioRenderBus render_bus(2, 32, 2, 4);
  sar::platform::VirtualAsioCaptureBus capture_bus(2, 32, 2, 4);
  sar::service::WindowsVirtualAsioTransportHost host({
      .endpoint_token = "host-smoke", .maximum_clients = 2,
      .queue_capacity_blocks = 4, .wait_timeout_ms = 10},
      &render_bus, &capture_bus);
  const auto initial_graph_generation = host.graph_generation();
  auto first =
      host.connect(request("daw-a"), make_graph(1), initial_graph_generation);
  assert(first.ok());
  assert(render_bus.stats().active_producers == 1);
  assert(capture_bus.stats().active_consumers == 1);
  auto mapping = sar::platform::WindowsVirtualAsioSharedMemory::open(
      first.connection().names.mapping);
  assert(mapping.ok());
  assert(mapping.mapping().state() ==
         sar::platform::VirtualAsioSharedMemoryState::Ready);

  assert(!host.connect(request("bad-rate", 44100), make_graph(2),
                       host.graph_generation()).ok());
  assert(!host.connect(request("bad-graph"), make_graph(3, 1),
                       host.graph_generation()).ok());
  auto second = host.connect(request("bad-graph"), make_graph(4),
                             host.graph_generation());
  assert(second.ok());
  assert(host.active_session_count() == 2);
  assert(render_bus.stats().active_producers == 2);
  assert(capture_bus.stats().active_consumers == 2);
  assert(second.connection().names.mapping != first.connection().names.mapping);
  assert(!host.connect(request("daw-c"), make_graph(5),
                       host.graph_generation()).ok());
  assert(render_bus.stats().active_producers == 2);
  assert(capture_bus.stats().active_consumers == 2);

  std::uint64_t next_version = 10;
  auto refreshed = host.refresh_graphs(
      [&](const sar::platform::VirtualAsioFormat&) {
        return make_graph(next_version++);
      });
  assert(refreshed.ok());
  assert(refreshed.updated_sessions == 2);
  assert(host.graph_generation() != initial_graph_generation);

  const auto stale =
      host.connect(request("stale-graph"), make_graph(11),
                   initial_graph_generation);
  assert(!stale.ok());
  assert(stale.errors()[0].code == "virtual_asio_graph_generation_stale");
  assert(host.active_session_count() == 2);

  const auto generation_before_rejected_refresh = host.graph_generation();
  std::size_t build_index = 0;
  const auto rejected_refresh = host.refresh_graphs(
      [&](const sar::platform::VirtualAsioFormat&) {
        ++build_index;
        return make_graph(20 + build_index, build_index == 2 ? 1 : 2);
      });
  assert(!rejected_refresh.ok());
  assert(rejected_refresh.updated_sessions == 0);
  assert(rejected_refresh.errors[0].code ==
         "virtual_asio_graph_refresh_format_mismatch");
  assert(host.graph_generation() == generation_before_rejected_refresh);

  sar::service::WindowsVirtualAsioTransportHost empty_host({
      .endpoint_token = "empty-refresh", .maximum_clients = 1,
      .queue_capacity_blocks = 4, .wait_timeout_ms = 10});
  const auto empty_generation = empty_host.graph_generation();
  const auto empty_refresh = empty_host.refresh_graphs(
      [](const sar::platform::VirtualAsioFormat&) { return make_graph(30); });
  assert(empty_refresh.ok());
  assert(empty_refresh.updated_sessions == 0);
  assert(empty_host.graph_generation() != empty_generation);
  const auto stale_empty_connect = empty_host.connect(
      request("stale-empty"), make_graph(31), empty_generation);
  assert(!stale_empty_connect.ok());
  assert(empty_host.active_session_count() == 0);
  const auto retried_empty_connect = empty_host.connect(
      request("stale-empty"), make_graph(32), empty_host.graph_generation());
  assert(retried_empty_connect.ok());
  assert(empty_host.active_session_count() == 1);
  empty_host.stop_all();

  assert(!host.disconnect(
      "daw-a", first.connection().client.connection_generation + 1).ok());
  assert(host.active_session_count() == 2);
  assert(host.disconnect(
      "daw-a", first.connection().client.connection_generation).ok());
  assert(host.active_session_count() == 1);
  assert(render_bus.stats().active_producers == 1);
  assert(capture_bus.stats().active_consumers == 1);
  assert(host.connections().front().client.client_id == "bad-graph");
  host.stop_all();
  assert(host.active_session_count() == 0);
  assert(render_bus.stats().active_producers == 0);
  assert(capture_bus.stats().active_consumers == 0);
  assert(host.reap_stopped_sessions() == 0);
}
