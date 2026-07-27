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
  sar::service::WindowsVirtualAsioTransportHost host({
      .endpoint_token = "host-smoke", .maximum_clients = 2,
      .queue_capacity_blocks = 4, .wait_timeout_ms = 10});
  auto first = host.connect(request("daw-a"), make_graph(1));
  assert(first.ok());
  auto mapping = sar::platform::WindowsVirtualAsioSharedMemory::open(
      first.connection().names.mapping);
  assert(mapping.ok());
  assert(mapping.mapping().state() ==
         sar::platform::VirtualAsioSharedMemoryState::Ready);

  assert(!host.connect(request("bad-rate", 44100), make_graph(2)).ok());
  assert(!host.connect(request("bad-graph"), make_graph(3, 1)).ok());
  auto second = host.connect(request("bad-graph"), make_graph(4));
  assert(second.ok());
  assert(host.active_session_count() == 2);
  assert(second.connection().names.mapping != first.connection().names.mapping);
  assert(!host.connect(request("daw-c"), make_graph(5)).ok());

  std::uint64_t next_version = 10;
  auto refreshed = host.refresh_graphs(
      [&](const sar::platform::VirtualAsioFormat&) {
        return make_graph(next_version++);
      });
  assert(refreshed.ok());
  assert(refreshed.updated_sessions == 2);

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

  assert(!host.disconnect(
      "daw-a", first.connection().client.connection_generation + 1).ok());
  assert(host.active_session_count() == 2);
  assert(host.disconnect(
      "daw-a", first.connection().client.connection_generation).ok());
  assert(host.active_session_count() == 1);
  assert(host.connections().front().client.client_id == "bad-graph");
  host.stop_all();
  assert(host.active_session_count() == 0);
  assert(host.reap_stopped_sessions() == 0);
}
