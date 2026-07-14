cmake_minimum_required(VERSION 3.24)

project(SystemAudioRoute LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sar_libsamplerate STATIC
  third_party/libsamplerate/src/samplerate.c
  third_party/libsamplerate/src/src_linear.c
  third_party/libsamplerate/src/src_sinc.c
  third_party/libsamplerate/src/src_zoh.c
)

target_include_directories(sar_libsamplerate
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsamplerate/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsamplerate/src
)

target_compile_definitions(sar_libsamplerate PRIVATE
  CPU_CLIPS_NEGATIVE=0
  CPU_CLIPS_POSITIVE=0
  ENABLE_SINC_FAST_CONVERTER
  ENABLE_SINC_MEDIUM_CONVERTER
  HAVE_STDBOOL_H=1
  PACKAGE="libsamplerate"
  VERSION="0.2.2"
)

add_library(sar_core
  core/control/control_command.cpp
  core/control/preset_document.cpp
  core/control/control_response.cpp
  core/control/control_session.cpp
  core/platform/audio_device.cpp
  core/platform/audio_device_registry.cpp
  core/platform/mock_audio_device_provider.cpp
  core/platform/sample_converter.cpp
  core/platform/virtual_endpoint.cpp
  core/platform/wasapi_recovery_policy.cpp
  core/platform/wasapi_realtime_error.cpp
  core/realtime/adaptive_resampler.cpp
  core/realtime/audio_buffer.cpp
  core/realtime/fifo_waterline_controller.cpp
  core/realtime/planar_audio_fifo.cpp
  core/graph/graph.cpp
  core/graph/graph_builder.cpp
  core/graph/graph_snapshot.cpp
  core/graph/route_matrix.cpp
)

target_include_directories(sar_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(sar_core PUBLIC cxx_std_20)
target_link_libraries(sar_core PRIVATE sar_libsamplerate)

if(WIN32)
  target_sources(sar_core PRIVATE
    core/platform/windows_wasapi_device_provider.cpp
    core/platform/windows_wasapi_duplex_loop.cpp
    core/platform/windows_wasapi_duplex_supervisor.cpp
    core/platform/windows_wasapi_endpoint_notification.cpp
    core/platform/windows_wasapi_graph_runner.cpp
    core/platform/windows_wasapi_loopback_loop.cpp
    core/platform/windows_wasapi_loop_preflight.cpp
    core/platform/windows_wasapi_realtime_worker.cpp
    core/platform/windows_wasapi_render_loop.cpp
    core/platform/windows_wasapi_runtime_summary.cpp
    core/platform/windows_realtime_thread.cpp
    core/platform/windows_wasapi_stream.cpp
    core/platform/windows_wasapi_stream_probe.cpp
  )
  target_link_libraries(sar_core PUBLIC avrt ole32 uuid)
endif()

add_executable(sar_realtime_smoke
  tests/realtime/realtime_smoke.cpp
)

target_link_libraries(sar_realtime_smoke PRIVATE sar_core)

add_executable(sar_atomic_float_parameter_smoke
  tests/realtime/atomic_float_parameter_smoke.cpp
)

target_link_libraries(sar_atomic_float_parameter_smoke PRIVATE sar_core)

add_executable(sar_meter_node_smoke
  tests/realtime/meter_node_smoke.cpp
)

target_link_libraries(sar_meter_node_smoke PRIVATE sar_core)

add_executable(sar_graph_snapshot_smoke
  tests/realtime/graph_snapshot_smoke.cpp
)

target_link_libraries(sar_graph_snapshot_smoke PRIVATE sar_core)

add_executable(sar_graph_builder_smoke
  tests/realtime/graph_builder_smoke.cpp
)

target_link_libraries(sar_graph_builder_smoke PRIVATE sar_core)

add_executable(sar_route_matrix_smoke
  tests/realtime/route_matrix_smoke.cpp
)

target_link_libraries(sar_route_matrix_smoke PRIVATE sar_core)

add_executable(sar_preset_document_smoke
  tests/realtime/preset_document_smoke.cpp
)

target_link_libraries(sar_preset_document_smoke PRIVATE sar_core)

add_executable(sar_control_command_smoke
  tests/realtime/control_command_smoke.cpp
)

target_link_libraries(sar_control_command_smoke PRIVATE sar_core)

add_executable(sar_control_response_smoke
  tests/realtime/control_response_smoke.cpp
)

target_link_libraries(sar_control_response_smoke PRIVATE sar_core)

add_executable(sar_control_session_smoke
  tests/realtime/control_session_smoke.cpp
)

target_link_libraries(sar_control_session_smoke PRIVATE sar_core)

add_executable(sar_audio_device_smoke
  tests/realtime/audio_device_smoke.cpp
)

target_link_libraries(sar_audio_device_smoke PRIVATE sar_core)

add_executable(sar_audio_device_registry_smoke
  tests/realtime/audio_device_registry_smoke.cpp
)

target_link_libraries(sar_audio_device_registry_smoke PRIVATE sar_core)

add_executable(sar_virtual_endpoint_smoke
  tests/realtime/virtual_endpoint_smoke.cpp
)

target_link_libraries(sar_virtual_endpoint_smoke PRIVATE sar_core)

add_executable(sar_sample_converter_smoke
  tests/realtime/sample_converter_smoke.cpp
)

target_link_libraries(sar_sample_converter_smoke PRIVATE sar_core)

add_executable(sar_wasapi_recovery_policy_smoke
  tests/realtime/wasapi_recovery_policy_smoke.cpp
)

target_link_libraries(sar_wasapi_recovery_policy_smoke PRIVATE sar_core)

add_executable(sar_wasapi_realtime_error_smoke
  tests/realtime/wasapi_realtime_error_smoke.cpp
)

target_link_libraries(sar_wasapi_realtime_error_smoke PRIVATE sar_core)

if(WIN32)
  add_executable(sar_windows_wasapi_device_smoke
    tests/realtime/windows_wasapi_device_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_device_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_endpoint_notification_smoke
    tests/realtime/windows_wasapi_endpoint_notification_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_endpoint_notification_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_stream_probe_smoke
    tests/realtime/windows_wasapi_stream_probe_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_stream_probe_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_stream_smoke
    tests/realtime/windows_wasapi_stream_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_stream_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_graph_runner_smoke
    tests/realtime/windows_wasapi_graph_runner_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_graph_runner_smoke PRIVATE sar_core)

  add_executable(sar_scripted_wasapi_stream_smoke
    tests/realtime/scripted_wasapi_stream_smoke.cpp
  )

  target_link_libraries(sar_scripted_wasapi_stream_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_fifo_runner_smoke
    tests/realtime/windows_wasapi_fifo_runner_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_fifo_runner_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_adaptive_capture_smoke
    tests/realtime/windows_wasapi_adaptive_capture_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_adaptive_capture_smoke PRIVATE sar_core)

  add_executable(sar_wasapi_adaptive_gap_smoke
    tests/realtime/windows_wasapi_adaptive_capture_discontinuity_smoke.cpp
  )

  target_link_libraries(sar_wasapi_adaptive_gap_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_buffered_cancel_idle_smoke
    tests/realtime/windows_wasapi_buffered_cancel_idle_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_buffered_cancel_idle_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_loop_preflight_smoke
    tests/realtime/windows_wasapi_loop_preflight_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_loop_preflight_smoke PRIVATE sar_core)

  add_executable(sar_windows_realtime_thread_smoke
    tests/realtime/windows_realtime_thread_smoke.cpp
  )

  target_link_libraries(sar_windows_realtime_thread_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_realtime_worker_smoke
    tests/realtime/windows_wasapi_realtime_worker_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_realtime_worker_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_runtime_summary_smoke
    tests/realtime/windows_wasapi_runtime_summary_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_runtime_summary_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_render_loop_smoke
    tests/realtime/windows_wasapi_render_loop_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_render_loop_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_duplex_loop_smoke
    tests/realtime/windows_wasapi_duplex_loop_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_duplex_loop_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_duplex_supervisor_smoke
    tests/realtime/windows_wasapi_duplex_supervisor_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_duplex_supervisor_smoke PRIVATE sar_core)

  add_executable(sar_windows_wasapi_loopback_loop_smoke
    tests/realtime/windows_wasapi_loopback_loop_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_loopback_loop_smoke PRIVATE sar_core)

  add_executable(sar_list_wasapi_devices
    tools/list_wasapi_devices.cpp
  )

  target_link_libraries(sar_list_wasapi_devices PRIVATE sar_core)

  add_executable(sar_measure_wasapi_render_loop
    tools/measure_wasapi_render_loop.cpp
    tools/wasapi_measure_options.cpp
    tools/wasapi_measure_report.cpp
  )

  target_link_libraries(sar_measure_wasapi_render_loop PRIVATE sar_core)

  add_executable(sar_measure_wasapi_duplex_loop
    tools/measure_wasapi_duplex_loop.cpp
    tools/wasapi_measure_options.cpp
    tools/wasapi_measure_report.cpp
  )

  target_link_libraries(sar_measure_wasapi_duplex_loop PRIVATE sar_core)

  add_executable(sar_measure_wasapi_loopback_loop
    tools/measure_wasapi_loopback_loop.cpp
    tools/wasapi_measure_options.cpp
    tools/wasapi_measure_report.cpp
  )

  target_link_libraries(sar_measure_wasapi_loopback_loop PRIVATE sar_core)

  add_executable(sar_wasapi_measure_options_smoke
    tests/realtime/wasapi_measure_options_smoke.cpp
    tools/wasapi_measure_options.cpp
  )

  target_link_libraries(sar_wasapi_measure_options_smoke PRIVATE sar_core)

  add_executable(sar_wasapi_measure_report_smoke
    tests/realtime/wasapi_measure_report_smoke.cpp
    tools/wasapi_measure_report.cpp
  )

  target_link_libraries(sar_wasapi_measure_report_smoke PRIVATE sar_core)
endif()

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

add_executable(sar_audio_block_timeline_smoke
  tests/realtime/audio_block_timeline_smoke.cpp
)

target_link_libraries(sar_audio_block_timeline_smoke PRIVATE sar_core)

add_executable(sar_clock_drift_estimator_smoke
  tests/realtime/clock_drift_estimator_smoke.cpp
)

target_link_libraries(sar_clock_drift_estimator_smoke PRIVATE sar_core)

add_executable(sar_planar_audio_fifo_smoke
  tests/realtime/planar_audio_fifo_smoke.cpp
)

target_link_libraries(sar_planar_audio_fifo_smoke PRIVATE sar_core)

add_executable(sar_fifo_waterline_controller_smoke
  tests/realtime/fifo_waterline_controller_smoke.cpp
)

target_link_libraries(sar_fifo_waterline_controller_smoke PRIVATE sar_core)

add_executable(sar_fifo_waterline_gap_simulation_smoke
  tests/realtime/fifo_waterline_discontinuity_simulation_smoke.cpp
)

target_link_libraries(sar_fifo_waterline_gap_simulation_smoke PRIVATE sar_core)

add_executable(sar_adaptive_resampler_smoke
  tests/realtime/adaptive_resampler_smoke.cpp
)

target_link_libraries(sar_adaptive_resampler_smoke PRIVATE sar_core)

add_executable(sar_adaptive_resampler_streaming_smoke
  tests/realtime/adaptive_resampler_streaming_smoke.cpp
)

target_link_libraries(sar_adaptive_resampler_streaming_smoke PRIVATE sar_core)

add_executable(sar_xrun_detection_smoke
  tests/realtime/xrun_detection_smoke.cpp
)

target_link_libraries(sar_xrun_detection_smoke PRIVATE sar_core)

add_executable(sar_spsc_ring_buffer_threaded_smoke
  tests/realtime/spsc_ring_buffer_threaded_smoke.cpp
)

target_link_libraries(sar_spsc_ring_buffer_threaded_smoke PRIVATE sar_core)

enable_testing()
add_test(NAME realtime_smoke COMMAND sar_realtime_smoke)
add_test(NAME atomic_float_parameter_smoke COMMAND sar_atomic_float_parameter_smoke)
add_test(NAME meter_node_smoke COMMAND sar_meter_node_smoke)
add_test(NAME graph_snapshot_smoke COMMAND sar_graph_snapshot_smoke)
add_test(NAME graph_builder_smoke COMMAND sar_graph_builder_smoke)
add_test(NAME route_matrix_smoke COMMAND sar_route_matrix_smoke)
add_test(NAME preset_document_smoke COMMAND sar_preset_document_smoke)
add_test(NAME control_command_smoke COMMAND sar_control_command_smoke)
add_test(NAME control_response_smoke COMMAND sar_control_response_smoke)
add_test(NAME control_session_smoke COMMAND sar_control_session_smoke)
add_test(NAME audio_device_smoke COMMAND sar_audio_device_smoke)
add_test(NAME audio_device_registry_smoke COMMAND sar_audio_device_registry_smoke)
add_test(NAME virtual_endpoint_smoke COMMAND sar_virtual_endpoint_smoke)
add_test(NAME sample_converter_smoke COMMAND sar_sample_converter_smoke)
add_test(NAME wasapi_recovery_policy_smoke COMMAND sar_wasapi_recovery_policy_smoke)
add_test(NAME wasapi_realtime_error_smoke COMMAND sar_wasapi_realtime_error_smoke)
if(WIN32)
  add_test(NAME windows_wasapi_device_smoke COMMAND sar_windows_wasapi_device_smoke)
  add_test(NAME windows_wasapi_endpoint_notification_smoke
           COMMAND sar_windows_wasapi_endpoint_notification_smoke)
  add_test(NAME windows_wasapi_stream_probe_smoke
           COMMAND sar_windows_wasapi_stream_probe_smoke)
  add_test(NAME windows_wasapi_stream_smoke COMMAND sar_windows_wasapi_stream_smoke)
  add_test(NAME windows_wasapi_graph_runner_smoke
           COMMAND sar_windows_wasapi_graph_runner_smoke)
  add_test(NAME scripted_wasapi_stream_smoke
           COMMAND sar_scripted_wasapi_stream_smoke)
  add_test(NAME windows_wasapi_fifo_runner_smoke
    COMMAND sar_windows_wasapi_fifo_runner_smoke)
  add_test(NAME windows_wasapi_adaptive_capture_smoke
    COMMAND sar_windows_wasapi_adaptive_capture_smoke)
  add_test(NAME windows_wasapi_adaptive_capture_discontinuity_smoke
    COMMAND sar_wasapi_adaptive_gap_smoke)
  add_test(NAME windows_wasapi_buffered_cancel_idle_smoke
           COMMAND sar_windows_wasapi_buffered_cancel_idle_smoke)
  add_test(NAME windows_wasapi_loop_preflight_smoke
           COMMAND sar_windows_wasapi_loop_preflight_smoke)
  add_test(NAME windows_realtime_thread_smoke COMMAND sar_windows_realtime_thread_smoke)
  add_test(NAME windows_wasapi_realtime_worker_smoke
           COMMAND sar_windows_wasapi_realtime_worker_smoke)
  add_test(NAME windows_wasapi_runtime_summary_smoke
           COMMAND sar_windows_wasapi_runtime_summary_smoke)
  add_test(NAME windows_wasapi_render_loop_smoke
           COMMAND sar_windows_wasapi_render_loop_smoke)
  add_test(NAME windows_wasapi_duplex_loop_smoke
    COMMAND sar_windows_wasapi_duplex_loop_smoke)
  add_test(NAME windows_wasapi_duplex_supervisor_smoke
    COMMAND sar_windows_wasapi_duplex_supervisor_smoke)
  add_test(NAME windows_wasapi_loopback_loop_smoke
    COMMAND sar_windows_wasapi_loopback_loop_smoke)
  add_test(NAME windows_wasapi_render_loop_measure_help
    COMMAND sar_measure_wasapi_render_loop --help)
  add_test(NAME windows_wasapi_render_loop_measure_short_help
    COMMAND sar_measure_wasapi_render_loop -h)
  add_test(NAME windows_wasapi_render_loop_measure_missing_duration
    COMMAND sar_measure_wasapi_render_loop --duration-ms)
  add_test(NAME windows_wasapi_render_loop_measure_bad_duration
    COMMAND sar_measure_wasapi_render_loop --duration-ms abc)
  add_test(NAME windows_wasapi_render_loop_measure_bad_timeout
    COMMAND sar_measure_wasapi_render_loop --timeout-ms -1)
  add_test(NAME windows_wasapi_render_loop_measure_unknown_arg
    COMMAND sar_measure_wasapi_render_loop --definitely-not-a-real-option)
  add_test(NAME windows_wasapi_duplex_loop_measure_help
    COMMAND sar_measure_wasapi_duplex_loop --help)
  add_test(NAME windows_wasapi_duplex_loop_measure_short_help
    COMMAND sar_measure_wasapi_duplex_loop -h)
  add_test(NAME windows_wasapi_duplex_loop_measure_missing_duration
    COMMAND sar_measure_wasapi_duplex_loop --duration-ms)
  add_test(NAME windows_wasapi_duplex_loop_measure_bad_duration
    COMMAND sar_measure_wasapi_duplex_loop --duration-ms abc)
  add_test(NAME windows_wasapi_duplex_loop_measure_bad_timeout
    COMMAND sar_measure_wasapi_duplex_loop --timeout-ms -1)
  add_test(NAME windows_wasapi_duplex_loop_measure_unknown_arg
    COMMAND sar_measure_wasapi_duplex_loop --definitely-not-a-real-option)
  add_test(NAME windows_wasapi_loopback_loop_measure_help
    COMMAND sar_measure_wasapi_loopback_loop --help)
  add_test(NAME windows_wasapi_loopback_loop_measure_short_help
    COMMAND sar_measure_wasapi_loopback_loop -h)
  add_test(NAME windows_wasapi_loopback_loop_measure_missing_duration
    COMMAND sar_measure_wasapi_loopback_loop --duration-ms)
  add_test(NAME windows_wasapi_loopback_loop_measure_bad_duration
    COMMAND sar_measure_wasapi_loopback_loop --duration-ms abc)
  add_test(NAME windows_wasapi_loopback_loop_measure_bad_timeout
    COMMAND sar_measure_wasapi_loopback_loop --timeout-ms -1)
  add_test(NAME windows_wasapi_loopback_loop_measure_unknown_arg
    COMMAND sar_measure_wasapi_loopback_loop --definitely-not-a-real-option)
  add_test(NAME wasapi_measure_options_smoke
    COMMAND sar_wasapi_measure_options_smoke)
  add_test(NAME wasapi_measure_report_smoke
    COMMAND sar_wasapi_measure_report_smoke)
  set_tests_properties(
    windows_wasapi_render_loop_measure_missing_duration
    windows_wasapi_render_loop_measure_bad_duration
    windows_wasapi_render_loop_measure_bad_timeout
    windows_wasapi_render_loop_measure_unknown_arg
    windows_wasapi_duplex_loop_measure_missing_duration
    windows_wasapi_duplex_loop_measure_bad_duration
    windows_wasapi_duplex_loop_measure_bad_timeout
    windows_wasapi_duplex_loop_measure_unknown_arg
    windows_wasapi_loopback_loop_measure_missing_duration
    windows_wasapi_loopback_loop_measure_bad_duration
    windows_wasapi_loopback_loop_measure_bad_timeout
    windows_wasapi_loopback_loop_measure_unknown_arg
    PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(
    windows_wasapi_render_loop_measure_help
    windows_wasapi_render_loop_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--require-healthy")
  set_tests_properties(
    windows_wasapi_duplex_loop_measure_help
    windows_wasapi_duplex_loop_measure_short_help
    windows_wasapi_loopback_loop_measure_help
    windows_wasapi_loopback_loop_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--require-healthy")
endif()
add_test(NAME diagnostics_smoke COMMAND sar_diagnostics_smoke)
add_test(NAME spsc_ring_buffer_smoke COMMAND sar_spsc_ring_buffer_smoke)
add_test(NAME process_context_smoke COMMAND sar_process_context_smoke)
add_test(NAME audio_block_timeline_smoke COMMAND sar_audio_block_timeline_smoke)
add_test(NAME clock_drift_estimator_smoke COMMAND sar_clock_drift_estimator_smoke)
add_test(NAME planar_audio_fifo_smoke COMMAND sar_planar_audio_fifo_smoke)
add_test(NAME fifo_waterline_controller_smoke COMMAND sar_fifo_waterline_controller_smoke)
add_test(NAME fifo_waterline_gap_simulation_smoke
  COMMAND sar_fifo_waterline_gap_simulation_smoke)
add_test(NAME adaptive_resampler_smoke COMMAND sar_adaptive_resampler_smoke)
add_test(NAME adaptive_resampler_streaming_smoke
  COMMAND sar_adaptive_resampler_streaming_smoke)
add_test(NAME xrun_detection_smoke COMMAND sar_xrun_detection_smoke)
add_test(NAME spsc_ring_buffer_threaded_smoke COMMAND sar_spsc_ring_buffer_threaded_smoke)
