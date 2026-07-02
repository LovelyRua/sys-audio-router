cmake_minimum_required(VERSION 3.24)

project(SystemAudioRoute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sar_core
  core/realtime/audio_buffer.cpp
  core/graph/graph.cpp
  core/graph/graph_snapshot.cpp
  core/graph/route_matrix.cpp
)

target_include_directories(sar_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(sar_core PUBLIC cxx_std_20)

add_executable(sar_realtime_smoke
  tests/realtime/realtime_smoke.cpp
)

target_link_libraries(sar_realtime_smoke PRIVATE sar_core)

add_executable(sar_graph_snapshot_smoke
  tests/realtime/graph_snapshot_smoke.cpp
)

target_link_libraries(sar_graph_snapshot_smoke PRIVATE sar_core)

add_executable(sar_route_matrix_smoke
  tests/realtime/route_matrix_smoke.cpp
)

target_link_libraries(sar_route_matrix_smoke PRIVATE sar_core)

add_executable(sar_diagnostics_smoke
  tests/realtime/diagnostics_smoke.cpp
)

target_link_libraries(sar_diagnostics_smoke PRIVATE sar_core)

add_executable(sar_spsc_ring_buffer_smoke
  tests/realtime/spsc_ring_buffer_smoke.cpp
)

target_link_libraries(sar_spsc_ring_buffer_smoke PRIVATE sar_core)

add_executable(sar_process_context_smoke
  tests/realtime/process_context_smoke.cpp
)

target_link_libraries(sar_process_context_smoke PRIVATE sar_core)

add_executable(sar_spsc_ring_buffer_threaded_smoke
  tests/realtime/spsc_ring_buffer_threaded_smoke.cpp
)

target_link_libraries(sar_spsc_ring_buffer_threaded_smoke PRIVATE sar_core)

enable_testing()
add_test(NAME realtime_smoke COMMAND sar_realtime_smoke)
add_test(NAME graph_snapshot_smoke COMMAND sar_graph_snapshot_smoke)
add_test(NAME route_matrix_smoke COMMAND sar_route_matrix_smoke)
add_test(NAME diagnostics_smoke COMMAND sar_diagnostics_smoke)
add_test(NAME spsc_ring_buffer_smoke COMMAND sar_spsc_ring_buffer_smoke)
add_test(NAME process_context_smoke COMMAND sar_process_context_smoke)
add_test(NAME spsc_ring_buffer_threaded_smoke COMMAND sar_spsc_ring_buffer_threaded_smoke)
