cmake_minimum_required(VERSION 3.24)

project(SystemAudioRoute VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SAR_BUILD_GUI "Build the Qt Quick control application" OFF)

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
  core/control/preset_file_codec.cpp
  core/control/preset_document.cpp
  core/control/control_response.cpp
  core/control/control_session.cpp
  core/control/control_wire_protocol.cpp
  core/control/virtual_asio_broker_protocol.cpp
  core/control/session_document.cpp
  core/control/session_file_codec.cpp
  core/platform/audio_device.cpp
  core/platform/audio_device_registry.cpp
  core/platform/realtime_audio_fanout_sink.cpp
  core/platform/realtime_audio_channel_slice_sink.cpp
  core/platform/realtime_audio_endpoint_queue.cpp
  core/platform/realtime_audio_rate_matching_source.cpp
  core/platform/mock_asio_transport.cpp
  core/platform/mock_audio_device_provider.cpp
  core/platform/sample_converter.cpp
  core/platform/virtual_asio_client_registry.cpp
  core/platform/virtual_asio_capture_bus.cpp
  core/platform/virtual_asio_render_bus.cpp
  core/platform/virtual_asio_shared_memory_layout.cpp
  core/platform/virtual_endpoint.cpp
  core/platform/wasapi_endpoint_selection_policy.cpp
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
  core/service/engine_audio_runtime.cpp
  core/service/audio_runtime_topology.cpp
  core/service/audio_runtime_matrix_binding.cpp
  core/service/multi_endpoint_audio_runtime.cpp
  core/service/engine_control_service.cpp
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
    core/platform/windows_virtual_asio_events.cpp
    core/platform/windows_virtual_asio_object_names.cpp
    core/platform/windows_virtual_asio_security.cpp
    core/platform/windows_virtual_asio_shared_queue.cpp
    core/platform/windows_virtual_asio_shared_memory.cpp
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
    core/service/windows_wasapi_engine_runtime.cpp
    core/service/windows_named_pipe_control.cpp
    core/service/windows_virtual_asio_broker_client.cpp
    core/service/windows_virtual_asio_broker_server.cpp
    core/service/windows_virtual_asio_transport_session.cpp
    core/service/windows_virtual_asio_transport_host.cpp
  )
  target_link_libraries(sar_core PUBLIC advapi32 avrt bcrypt ole32 uuid)
endif()

if(SAR_BUILD_GUI)
  if(NOT WIN32)
    message(FATAL_ERROR "The alpha GUI control client currently requires Windows")
  endif()
  find_package(Qt6 6.8 REQUIRED COMPONENTS Concurrent Quick QuickControls2)
  qt_standard_project_setup(REQUIRES 6.8)
  add_subdirectory(app/gui)
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

add_executable(sar_bus_polarity_node_smoke
  tests/realtime/bus_polarity_node_smoke.cpp
)

target_link_libraries(sar_bus_polarity_node_smoke PRIVATE sar_core)

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

add_executable(sar_preset_file_codec_smoke
  tests/realtime/preset_file_codec_smoke.cpp
)

target_link_libraries(sar_preset_file_codec_smoke PRIVATE sar_core)

add_executable(sar_session_file_codec_smoke
  tests/realtime/session_file_codec_smoke.cpp
)

target_link_libraries(sar_session_file_codec_smoke PRIVATE sar_core)

add_executable(sar_control_command_smoke
  tests/realtime/control_command_smoke.cpp
)

target_link_libraries(sar_control_command_smoke PRIVATE sar_core)

add_executable(sar_audio_runtime_topology_smoke
  tests/realtime/audio_runtime_topology_smoke.cpp
)

target_link_libraries(sar_audio_runtime_topology_smoke PRIVATE sar_core)

add_executable(sar_audio_runtime_matrix_binding_smoke
  tests/realtime/audio_runtime_matrix_binding_smoke.cpp
)

target_link_libraries(sar_audio_runtime_matrix_binding_smoke PRIVATE sar_core)

add_executable(sar_multi_endpoint_audio_runtime_smoke
  tests/realtime/multi_endpoint_audio_runtime_smoke.cpp
)

target_link_libraries(sar_multi_endpoint_audio_runtime_smoke PRIVATE sar_core)

add_executable(sar_realtime_audio_fanout_sink_smoke
  tests/realtime/realtime_audio_fanout_sink_smoke.cpp
)

target_link_libraries(sar_realtime_audio_fanout_sink_smoke PRIVATE sar_core)

add_executable(sar_realtime_audio_channel_slice_sink_smoke
  tests/realtime/realtime_audio_channel_slice_sink_smoke.cpp
)

target_link_libraries(sar_realtime_audio_channel_slice_sink_smoke PRIVATE sar_core)

add_executable(sar_realtime_audio_endpoint_queue_smoke
  tests/realtime/realtime_audio_endpoint_queue_smoke.cpp
)

target_link_libraries(sar_realtime_audio_endpoint_queue_smoke PRIVATE sar_core)

add_executable(sar_realtime_audio_rate_matching_source_smoke
  tests/realtime/realtime_audio_rate_matching_source_smoke.cpp
)

target_link_libraries(sar_realtime_audio_rate_matching_source_smoke PRIVATE sar_core)

add_executable(sar_control_response_smoke
  tests/realtime/control_response_smoke.cpp
)

target_link_libraries(sar_control_response_smoke PRIVATE sar_core)

add_executable(sar_control_session_smoke
  tests/realtime/control_session_smoke.cpp
)

target_link_libraries(sar_control_session_smoke PRIVATE sar_core)

add_executable(sar_control_wire_protocol_smoke
  tests/realtime/control_wire_protocol_smoke.cpp
)

target_link_libraries(sar_control_wire_protocol_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_broker_protocol_smoke
  tests/realtime/virtual_asio_broker_protocol_smoke.cpp
)

target_link_libraries(sar_virtual_asio_broker_protocol_smoke PRIVATE sar_core)

add_executable(sar_engine_control_service_smoke
  tests/realtime/engine_control_service_smoke.cpp
)

target_link_libraries(sar_engine_control_service_smoke PRIVATE sar_core)

add_executable(sar_mock_asio_transport_smoke
  tests/realtime/mock_asio_transport_smoke.cpp
)

target_link_libraries(sar_mock_asio_transport_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_client_registry_smoke
  tests/realtime/virtual_asio_client_registry_smoke.cpp
)

target_link_libraries(sar_virtual_asio_client_registry_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_client_registry_stress_smoke
  tests/realtime/virtual_asio_client_registry_stress_smoke.cpp
)

target_link_libraries(sar_virtual_asio_client_registry_stress_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_shared_memory_layout_smoke
  tests/realtime/virtual_asio_shared_memory_layout_smoke.cpp
)

target_link_libraries(sar_virtual_asio_shared_memory_layout_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_render_bus_smoke
  tests/realtime/virtual_asio_render_bus_smoke.cpp
)

target_link_libraries(sar_virtual_asio_render_bus_smoke PRIVATE sar_core)

add_executable(sar_virtual_asio_capture_bus_smoke
  tests/realtime/virtual_asio_capture_bus_smoke.cpp
)

target_link_libraries(sar_virtual_asio_capture_bus_smoke PRIVATE sar_core)

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

add_executable(sar_wasapi_endpoint_selection_policy_smoke
  tests/realtime/wasapi_endpoint_selection_policy_smoke.cpp
)

target_link_libraries(sar_wasapi_endpoint_selection_policy_smoke PRIVATE sar_core)

add_executable(sar_wasapi_recovery_policy_smoke
  tests/realtime/wasapi_recovery_policy_smoke.cpp
)

target_link_libraries(sar_wasapi_recovery_policy_smoke PRIVATE sar_core)

add_executable(sar_wasapi_realtime_error_smoke
  tests/realtime/wasapi_realtime_error_smoke.cpp
)

target_link_libraries(sar_wasapi_realtime_error_smoke PRIVATE sar_core)

if(WIN32)
  add_library(sar_virtual_asio_registration STATIC
    driver/windows_virtual_asio_registration.cpp
  )
  target_include_directories(sar_virtual_asio_registration
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(sar_virtual_asio_registration PUBLIC cxx_std_20)
  target_link_libraries(sar_virtual_asio_registration PUBLIC advapi32)

  add_library(sar_virtual_asio_driver SHARED
    driver/windows_virtual_asio_driver.cpp
    driver/windows_virtual_asio_runtime.cpp
    driver/windows_virtual_asio_driver.def
  )
  target_include_directories(sar_virtual_asio_driver PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_features(sar_virtual_asio_driver PRIVATE cxx_std_20)
  target_link_libraries(sar_virtual_asio_driver PRIVATE sar_core ole32 bcrypt)
  set_target_properties(sar_virtual_asio_driver PROPERTIES
    OUTPUT_NAME "SystemAudioRouteVirtualASIO"
    PREFIX ""
  )

  add_executable(sar_virtual_asio_register
    tools/sar_virtual_asio_register.cpp
  )
  target_link_libraries(sar_virtual_asio_register
    PRIVATE sar_virtual_asio_registration rstrtmgr)

  add_executable(sar_virtual_asio_host_probe
    tools/sar_virtual_asio_host_probe.cpp
  )
  target_include_directories(sar_virtual_asio_host_probe
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(sar_virtual_asio_host_probe PRIVATE ole32)

  add_executable(sar_virtual_asio_soak
    tools/sar_virtual_asio_soak.cpp
    tools/virtual_asio_soak_options.cpp
  )
  target_link_libraries(sar_virtual_asio_soak PRIVATE sar_core)

  add_executable(sar_virtual_asio_soak_options_smoke
    tests/realtime/virtual_asio_soak_options_smoke.cpp
    tools/virtual_asio_soak_options.cpp
  )
  target_link_libraries(sar_virtual_asio_soak_options_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_soak_smoke
    tests/realtime/windows_virtual_asio_soak_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_soak_smoke PRIVATE sar_core)

  add_executable(sar_engine_service
    tools/sar_engine_service.cpp
  )

  target_link_libraries(sar_engine_service PRIVATE sar_core)

  add_executable(sar_bootstrap_launcher WIN32
    tools/sar_bootstrap_launcher.cpp
  )
  target_compile_features(sar_bootstrap_launcher PRIVATE cxx_std_20)
  target_link_libraries(sar_bootstrap_launcher PRIVATE user32)
  set_target_properties(sar_bootstrap_launcher PROPERTIES
    OUTPUT_NAME "SystemAudioRouteLauncher"
  )

  add_executable(sar_control_cli
    tools/sar_control_cli.cpp
  )

  target_link_libraries(sar_control_cli PRIVATE sar_core)

  add_executable(sar_measure_wasapi_capture_level
    tools/measure_wasapi_capture_level.cpp
  )

  target_link_libraries(sar_measure_wasapi_capture_level PRIVATE sar_core)

  add_executable(sar_windows_named_pipe_control_smoke
    tests/realtime/windows_named_pipe_control_smoke.cpp
  )

  target_link_libraries(sar_windows_named_pipe_control_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_com_smoke
    tests/realtime/windows_virtual_asio_com_smoke.cpp
  )
  target_include_directories(sar_windows_virtual_asio_com_smoke
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(sar_windows_virtual_asio_com_smoke PRIVATE sar_core ole32)

  add_executable(sar_windows_virtual_asio_registration_smoke
    tests/realtime/windows_virtual_asio_registration_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_registration_smoke
    PRIVATE sar_virtual_asio_registration)

  add_executable(sar_windows_virtual_asio_object_names_smoke
    tests/realtime/windows_virtual_asio_object_names_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_object_names_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_events_smoke
    tests/realtime/windows_virtual_asio_events_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_events_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_security_smoke
    tests/realtime/windows_virtual_asio_security_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_security_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_shared_memory_smoke
    tests/realtime/windows_virtual_asio_shared_memory_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_shared_memory_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_shared_queue_smoke
    tests/realtime/windows_virtual_asio_shared_queue_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_shared_queue_smoke PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_shared_queue_adversarial_smoke
    tests/realtime/windows_virtual_asio_shared_queue_adversarial_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_shared_queue_adversarial_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_transport_session_smoke
    tests/realtime/windows_virtual_asio_transport_session_smoke.cpp
  )

  target_link_libraries(sar_windows_virtual_asio_transport_session_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_transport_host_smoke
    tests/realtime/windows_virtual_asio_transport_host_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_transport_host_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_broker_server_smoke
    tests/realtime/windows_virtual_asio_broker_server_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_broker_server_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_broker_client_smoke
    tests/realtime/windows_virtual_asio_broker_client_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_broker_client_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_virtual_asio_restart_recovery_smoke
    tests/realtime/windows_virtual_asio_restart_recovery_smoke.cpp
  )
  target_link_libraries(sar_windows_virtual_asio_restart_recovery_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_engine_control_pipe_integration_smoke
    tests/realtime/windows_engine_control_pipe_integration_smoke.cpp
  )

  target_link_libraries(sar_windows_engine_control_pipe_integration_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_engine_virtual_asio_integration_smoke
    tests/realtime/windows_engine_virtual_asio_integration_smoke.cpp
  )
  target_link_libraries(sar_windows_engine_virtual_asio_integration_smoke
    PRIVATE sar_core)

  add_executable(sar_windows_wasapi_engine_runtime_smoke
    tests/realtime/windows_wasapi_engine_runtime_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_engine_runtime_smoke PRIVATE sar_core)

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

  add_executable(sar_windows_wasapi_external_input_smoke
    tests/realtime/windows_wasapi_external_input_smoke.cpp
  )

  target_link_libraries(sar_windows_wasapi_external_input_smoke PRIVATE sar_core)

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

  add_executable(sar_measure_wasapi_recovery
    tools/measure_wasapi_recovery.cpp
    tools/wasapi_measure_report.cpp
  )

  target_link_libraries(sar_measure_wasapi_recovery PRIVATE sar_core)

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
add_test(NAME bus_polarity_node_smoke COMMAND sar_bus_polarity_node_smoke)
add_test(NAME graph_snapshot_smoke COMMAND sar_graph_snapshot_smoke)
add_test(NAME graph_builder_smoke COMMAND sar_graph_builder_smoke)
add_test(NAME route_matrix_smoke COMMAND sar_route_matrix_smoke)
add_test(NAME preset_document_smoke COMMAND sar_preset_document_smoke)
add_test(NAME preset_file_codec_smoke COMMAND sar_preset_file_codec_smoke)
add_test(NAME session_file_codec_smoke COMMAND sar_session_file_codec_smoke)
add_test(NAME control_command_smoke COMMAND sar_control_command_smoke)
add_test(NAME audio_runtime_topology_smoke COMMAND sar_audio_runtime_topology_smoke)
add_test(NAME audio_runtime_matrix_binding_smoke
  COMMAND sar_audio_runtime_matrix_binding_smoke)
add_test(NAME multi_endpoint_audio_runtime_smoke
  COMMAND sar_multi_endpoint_audio_runtime_smoke)
add_test(NAME realtime_audio_fanout_sink_smoke
  COMMAND sar_realtime_audio_fanout_sink_smoke)
add_test(NAME realtime_audio_channel_slice_sink_smoke
  COMMAND sar_realtime_audio_channel_slice_sink_smoke)
add_test(NAME realtime_audio_endpoint_queue_smoke
  COMMAND sar_realtime_audio_endpoint_queue_smoke)
add_test(NAME realtime_audio_rate_matching_source_smoke
  COMMAND sar_realtime_audio_rate_matching_source_smoke)
add_test(NAME control_response_smoke COMMAND sar_control_response_smoke)
add_test(NAME control_session_smoke COMMAND sar_control_session_smoke)
add_test(NAME control_wire_protocol_smoke COMMAND sar_control_wire_protocol_smoke)
add_test(NAME virtual_asio_broker_protocol_smoke
  COMMAND sar_virtual_asio_broker_protocol_smoke)
add_test(NAME engine_control_service_smoke COMMAND sar_engine_control_service_smoke)
add_test(NAME mock_asio_transport_smoke COMMAND sar_mock_asio_transport_smoke)
add_test(NAME virtual_asio_client_registry_smoke
         COMMAND sar_virtual_asio_client_registry_smoke)
add_test(NAME virtual_asio_client_registry_stress_smoke
         COMMAND sar_virtual_asio_client_registry_stress_smoke)
add_test(NAME virtual_asio_shared_memory_layout_smoke
         COMMAND sar_virtual_asio_shared_memory_layout_smoke)
add_test(NAME virtual_asio_render_bus_smoke
         COMMAND sar_virtual_asio_render_bus_smoke)
add_test(NAME virtual_asio_capture_bus_smoke
         COMMAND sar_virtual_asio_capture_bus_smoke)
add_test(NAME audio_device_smoke COMMAND sar_audio_device_smoke)
add_test(NAME audio_device_registry_smoke COMMAND sar_audio_device_registry_smoke)
add_test(NAME virtual_endpoint_smoke COMMAND sar_virtual_endpoint_smoke)
add_test(NAME sample_converter_smoke COMMAND sar_sample_converter_smoke)
add_test(NAME wasapi_endpoint_selection_policy_smoke
         COMMAND sar_wasapi_endpoint_selection_policy_smoke)
add_test(NAME wasapi_recovery_policy_smoke COMMAND sar_wasapi_recovery_policy_smoke)
add_test(NAME wasapi_realtime_error_smoke COMMAND sar_wasapi_realtime_error_smoke)
if(WIN32)
  add_test(NAME windows_virtual_asio_com_smoke
           COMMAND sar_windows_virtual_asio_com_smoke
                   $<TARGET_FILE:sar_virtual_asio_driver>)
  add_test(NAME windows_virtual_asio_registration_smoke
           COMMAND sar_windows_virtual_asio_registration_smoke
                   $<TARGET_FILE:sar_virtual_asio_driver>)
  add_test(NAME windows_virtual_asio_registration_help
           COMMAND sar_virtual_asio_register --help)
  add_test(NAME windows_virtual_asio_driver_unlocked
           COMMAND sar_virtual_asio_register --check-unlocked
                   $<TARGET_FILE:sar_virtual_asio_driver>)
  add_test(NAME windows_virtual_asio_registration_missing_action
           COMMAND sar_virtual_asio_register)
  set_tests_properties(windows_virtual_asio_registration_missing_action
                       PROPERTIES WILL_FAIL TRUE)
  add_test(NAME windows_virtual_asio_object_names_smoke
           COMMAND sar_windows_virtual_asio_object_names_smoke)
  add_test(NAME windows_virtual_asio_events_smoke
           COMMAND sar_windows_virtual_asio_events_smoke)
  add_test(NAME windows_virtual_asio_security_smoke
           COMMAND sar_windows_virtual_asio_security_smoke)
  add_test(NAME windows_virtual_asio_shared_memory_smoke
           COMMAND sar_windows_virtual_asio_shared_memory_smoke)
  add_test(NAME windows_virtual_asio_shared_queue_smoke
           COMMAND sar_windows_virtual_asio_shared_queue_smoke)
  add_test(NAME windows_virtual_asio_shared_queue_adversarial_smoke
           COMMAND sar_windows_virtual_asio_shared_queue_adversarial_smoke)
  add_test(NAME windows_virtual_asio_transport_session_smoke
           COMMAND sar_windows_virtual_asio_transport_session_smoke)
  add_test(NAME windows_virtual_asio_transport_host_smoke
           COMMAND sar_windows_virtual_asio_transport_host_smoke)
  add_test(NAME windows_virtual_asio_broker_server_smoke
           COMMAND sar_windows_virtual_asio_broker_server_smoke)
  set_tests_properties(windows_virtual_asio_broker_server_smoke
                       PROPERTIES TIMEOUT 10)
  add_test(NAME windows_virtual_asio_broker_client_smoke
           COMMAND sar_windows_virtual_asio_broker_client_smoke)
  set_tests_properties(windows_virtual_asio_broker_client_smoke
                       PROPERTIES TIMEOUT 10)
  add_test(NAME virtual_asio_soak_options_smoke
           COMMAND sar_virtual_asio_soak_options_smoke)
  add_test(NAME windows_virtual_asio_soak_help
           COMMAND sar_virtual_asio_soak --help)
  set_tests_properties(windows_virtual_asio_soak_help
                       PROPERTIES PASS_REGULAR_EXPRESSION "--block-sizes")
  add_test(NAME windows_virtual_asio_soak_bad_clients
           COMMAND sar_virtual_asio_soak --clients 33)
  set_tests_properties(windows_virtual_asio_soak_bad_clients
                       PROPERTIES WILL_FAIL TRUE)
  add_test(NAME windows_virtual_asio_soak_smoke
           COMMAND sar_windows_virtual_asio_soak_smoke
                   $<TARGET_FILE:sar_virtual_asio_soak>)
  set_tests_properties(windows_virtual_asio_soak_smoke PROPERTIES TIMEOUT 15)
  add_test(NAME windows_virtual_asio_restart_recovery_smoke
           COMMAND sar_windows_virtual_asio_restart_recovery_smoke)
  set_tests_properties(windows_virtual_asio_restart_recovery_smoke
                       PROPERTIES TIMEOUT 15)
  add_test(NAME windows_named_pipe_control_smoke
           COMMAND sar_windows_named_pipe_control_smoke)
  set_tests_properties(windows_named_pipe_control_smoke PROPERTIES TIMEOUT 10)
  add_test(NAME windows_engine_control_pipe_integration_smoke
           COMMAND sar_windows_engine_control_pipe_integration_smoke)
  set_tests_properties(windows_engine_control_pipe_integration_smoke
                       PROPERTIES TIMEOUT 10)
  add_test(NAME windows_engine_virtual_asio_integration_smoke
           COMMAND sar_windows_engine_virtual_asio_integration_smoke
                   $<TARGET_FILE:sar_engine_service>)
  set_tests_properties(windows_engine_virtual_asio_integration_smoke
                       PROPERTIES TIMEOUT 20)
  add_test(NAME windows_wasapi_engine_runtime_smoke
           COMMAND sar_windows_wasapi_engine_runtime_smoke)
  add_test(NAME windows_engine_service_runtime_conflict
           COMMAND sar_engine_service --wasapi-render --wasapi-duplex)
  set_tests_properties(windows_engine_service_runtime_conflict
                       PROPERTIES WILL_FAIL TRUE TIMEOUT 5)
  add_test(NAME windows_engine_service_incomplete_duplex_ids
           COMMAND sar_engine_service --wasapi-duplex --capture-id capture-only)
  set_tests_properties(windows_engine_service_incomplete_duplex_ids
                       PROPERTIES WILL_FAIL TRUE TIMEOUT 5)
  add_test(NAME windows_wasapi_device_smoke COMMAND sar_windows_wasapi_device_smoke)
  add_test(NAME windows_wasapi_endpoint_notification_smoke
           COMMAND sar_windows_wasapi_endpoint_notification_smoke)
  add_test(NAME windows_wasapi_stream_probe_smoke
           COMMAND sar_windows_wasapi_stream_probe_smoke)
  add_test(NAME windows_wasapi_stream_smoke COMMAND sar_windows_wasapi_stream_smoke)
  add_test(NAME windows_wasapi_graph_runner_smoke
           COMMAND sar_windows_wasapi_graph_runner_smoke)
  add_test(NAME windows_wasapi_external_input_smoke
           COMMAND sar_windows_wasapi_external_input_smoke)
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
  add_test(NAME windows_wasapi_duplex_loop_measure_capture_id_only
    COMMAND sar_measure_wasapi_duplex_loop --capture-id capture-endpoint-id)
  add_test(NAME windows_wasapi_duplex_loop_measure_render_id_only
    COMMAND sar_measure_wasapi_duplex_loop --render-id render-endpoint-id)
  add_test(NAME windows_wasapi_loopback_loop_measure_help
    COMMAND sar_measure_wasapi_loopback_loop --help)
  add_test(NAME windows_wasapi_capture_level_measure_help
    COMMAND sar_measure_wasapi_capture_level --help)
  add_test(NAME windows_wasapi_capture_level_measure_missing_id
    COMMAND sar_measure_wasapi_capture_level)
  add_test(NAME windows_wasapi_capture_level_measure_bad_duration
    COMMAND sar_measure_wasapi_capture_level --capture-id dummy --duration-ms abc)
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
  add_test(NAME windows_wasapi_recovery_measure_help
    COMMAND sar_measure_wasapi_recovery --help)
  add_test(NAME windows_wasapi_recovery_measure_short_help
    COMMAND sar_measure_wasapi_recovery -h)
  add_test(NAME windows_wasapi_recovery_measure_missing_duration
    COMMAND sar_measure_wasapi_recovery --duration-ms)
  add_test(NAME windows_wasapi_recovery_measure_bad_duration
    COMMAND sar_measure_wasapi_recovery --duration-ms abc)
  add_test(NAME windows_wasapi_recovery_measure_zero_duration
    COMMAND sar_measure_wasapi_recovery --duration-ms 0)
  add_test(NAME windows_wasapi_recovery_measure_missing_poll
    COMMAND sar_measure_wasapi_recovery --poll-ms)
  add_test(NAME windows_wasapi_recovery_measure_bad_poll
    COMMAND sar_measure_wasapi_recovery --poll-ms abc)
  add_test(NAME windows_wasapi_recovery_measure_zero_poll
    COMMAND sar_measure_wasapi_recovery --poll-ms 0)
  add_test(NAME windows_wasapi_recovery_measure_missing_timeout
    COMMAND sar_measure_wasapi_recovery --timeout-ms)
  add_test(NAME windows_wasapi_recovery_measure_bad_timeout
    COMMAND sar_measure_wasapi_recovery --timeout-ms -1)
  add_test(NAME windows_wasapi_recovery_measure_zero_timeout
    COMMAND sar_measure_wasapi_recovery --timeout-ms 0)
  add_test(NAME windows_wasapi_recovery_measure_unknown_arg
    COMMAND sar_measure_wasapi_recovery --definitely-not-a-real-option)
  add_test(NAME windows_wasapi_recovery_measure_missing_capture_id
    COMMAND sar_measure_wasapi_recovery --capture-id)
  add_test(NAME windows_wasapi_recovery_measure_missing_render_id
    COMMAND sar_measure_wasapi_recovery --render-id)
  add_test(NAME windows_wasapi_recovery_measure_render_only_capture_conflict
    COMMAND sar_measure_wasapi_recovery --render-only --capture-id capture-id)
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
    windows_wasapi_duplex_loop_measure_capture_id_only
    windows_wasapi_duplex_loop_measure_render_id_only
    windows_wasapi_loopback_loop_measure_missing_duration
    windows_wasapi_loopback_loop_measure_bad_duration
    windows_wasapi_loopback_loop_measure_bad_timeout
    windows_wasapi_loopback_loop_measure_unknown_arg
    windows_wasapi_capture_level_measure_missing_id
    windows_wasapi_capture_level_measure_bad_duration
    windows_wasapi_recovery_measure_missing_duration
    windows_wasapi_recovery_measure_bad_duration
    windows_wasapi_recovery_measure_zero_duration
    windows_wasapi_recovery_measure_missing_poll
    windows_wasapi_recovery_measure_bad_poll
    windows_wasapi_recovery_measure_zero_poll
    windows_wasapi_recovery_measure_missing_timeout
    windows_wasapi_recovery_measure_bad_timeout
    windows_wasapi_recovery_measure_zero_timeout
    windows_wasapi_recovery_measure_unknown_arg
    windows_wasapi_recovery_measure_missing_capture_id
    windows_wasapi_recovery_measure_missing_render_id
    windows_wasapi_recovery_measure_render_only_capture_conflict
    PROPERTIES WILL_FAIL TRUE)
  set_tests_properties(
    windows_wasapi_render_loop_measure_help
    windows_wasapi_render_loop_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--require-healthy")
  set_tests_properties(
    windows_wasapi_loopback_loop_measure_help
    windows_wasapi_loopback_loop_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--require-healthy")
  set_tests_properties(
    windows_wasapi_duplex_loop_measure_help
    windows_wasapi_duplex_loop_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--require-healthy.*--capture-id ID --render-id ID")
  set_tests_properties(
    windows_wasapi_recovery_measure_help
    windows_wasapi_recovery_measure_short_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--poll-ms.*--render-only")
  set_tests_properties(
    windows_wasapi_capture_level_measure_help
    PROPERTIES PASS_REGULAR_EXPRESSION "--capture-id ID")
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

# Smoke tests use assert for executable checks. Keep those checks enabled in
# Release CI without changing assertion behavior in sar_core or shipping tools.
get_property(sar_directory_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(sar_target IN LISTS sar_directory_targets)
  if(sar_target MATCHES "^sar_.*_smoke$")
    if(MSVC)
      target_compile_options(${sar_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${sar_target} PRIVATE -UNDEBUG)
    endif()
  endif()
endforeach()

if(WIN32 AND SAR_BUILD_GUI)
  install(TARGETS
    sar_bootstrap_launcher
    sar_engine_service
    sar_control_cli
    sar_virtual_asio_driver
    sar_virtual_asio_register
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  if(SAR_BUILD_GUI)
    install(TARGETS sar_gui
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    qt_generate_deploy_qml_app_script(
      TARGET sar_gui
      OUTPUT_SCRIPT sar_gui_deploy_script
      NO_TRANSLATIONS
      NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${sar_gui_deploy_script})
  endif()
  install(FILES
    LICENSE
    NOTICE.md
    README.md
    DESTINATION .
  )
  install(FILES
    packaging/windows/install-alpha.ps1
    packaging/windows/uninstall-alpha.ps1
    DESTINATION .
  )
  install(PROGRAMS
    packaging/windows/install-alpha.cmd
    packaging/windows/uninstall-alpha.cmd
    DESTINATION .
  )

  set(CPACK_GENERATOR "ZIP;NSIS")
  set(CPACK_VERBATIM_VARIABLES ON)
  set(CPACK_PACKAGE_NAME "SystemAudioRoute")
  set(CPACK_PACKAGE_VENDOR "System Audio Route contributors")
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Low-latency Windows audio routing engine and control panel")
  set(CPACK_PACKAGE_HOMEPAGE_URL
      "https://github.com/LovelyRua/sys-audio-router")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
  set(CPACK_PACKAGE_FILE_NAME
      "SystemAudioRoute-${PROJECT_VERSION}-windows-x64")
  set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "System Audio Route")
  set(CPACK_PACKAGE_EXECUTABLES
      "SystemAudioRouteLauncher" "System Audio Route")
  set(CPACK_CREATE_DESKTOP_LINKS "SystemAudioRouteLauncher")

  set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
  set(CPACK_NSIS_DISPLAY_NAME "System Audio Route")
  set(CPACK_NSIS_PACKAGE_NAME "System Audio Route")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
  set(CPACK_NSIS_INSTALLED_ICON_NAME
      "bin\\\\SystemAudioRouteLauncher.exe")
  set(CPACK_NSIS_MUI_FINISHPAGE_RUN
      "bin\\\\SystemAudioRouteLauncher.exe")
  # CPack's NSIS template runs elevated. Hand the finish-page launch to the
  # existing Explorer process so the control panel starts in the signed-in
  # desktop session instead of inheriting the installer's elevated token.
  set(CPACK_NSIS_DEFINES [[
    !define MUI_FINISHPAGE_RUN_FUNCTION SarLaunchControlPanel
    Function SarLaunchControlPanel
      Exec '"$WINDIR\explorer.exe" "$INSTDIR\bin\SystemAudioRouteLauncher.exe"'
    FunctionEnd
  ]])
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_MANIFEST_DPI_AWARE ON)
  set(CPACK_NSIS_EXTRA_PREINSTALL_COMMANDS [[
    nsExec::ExecToStack '"$SYSDIR\cmd.exe" /D /C ""$SYSDIR\tasklist.exe" /M SystemAudioRouteVirtualASIO.dll /FO CSV /NH 2^>nul | "$SYSDIR\findstr.exe" /I /C:"SystemAudioRouteVirtualASIO.dll" ^>nul"'
    Pop $0
    Pop $1
    StrCmp $0 "0" 0 sar_asio_driver_unlocked
    MessageBox MB_OK|MB_ICONSTOP "System Audio Route Virtual ASIO is still loaded by an audio application.$\r$\n$\r$\nClose REAPER, your DAW, and plug-in scanners, then run this installer again.$\r$\n$\r$\nThe update has not changed any installed files."
    SetErrorLevel 1
    Abort
    sar_asio_driver_unlocked:
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM SystemAudioRoute.exe'
    Pop $0
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM sar_engine_service.exe'
    Pop $0
  ]])
  set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS [[
    ExecWait '"$INSTDIR\bin\vc_redist.x64.exe" /install /passive /norestart' $1
    StrCmp $1 "0" sar_vc_runtime_ok
    StrCmp $1 "1638" sar_vc_runtime_ok
    StrCmp $1 "3010" sar_vc_runtime_ok
    DetailPrint "Microsoft VC++ Runtime installation failed with exit code $1."
    SetErrorLevel 1
    Abort
    sar_vc_runtime_ok:
    ExecWait '"$INSTDIR\bin\sar_virtual_asio_register.exe" --register "$INSTDIR\bin\SystemAudioRouteVirtualASIO.dll" --user --x64' $0
    StrCmp $0 "0" sar_asio_registration_ok
    DetailPrint "Virtual ASIO registration failed with exit code $0."
    SetErrorLevel 1
    Abort
    sar_asio_registration_ok:
  ]])
  set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS [[
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM SystemAudioRoute.exe'
    Pop $0
    nsExec::ExecToLog '"$SYSDIR\taskkill.exe" /F /T /IM sar_engine_service.exe'
    Pop $0
    ExecWait '"$INSTDIR\bin\sar_virtual_asio_register.exe" --unregister-owned "$INSTDIR\bin\SystemAudioRouteVirtualASIO.dll" --user --x64' $0
    StrCmp $0 "0" sar_skip_asio_unregistration
    DetailPrint "Virtual ASIO unregistration failed with exit code $0."
    SetErrorLevel 1
    Abort
    sar_skip_asio_unregistration:
  ]])
  include(CPack)
endif()
