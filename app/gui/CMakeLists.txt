qt_add_executable(sar_gui WIN32
  engine_controller.cpp
  engine_controller.h
  main.cpp
  preset_store.cpp
  preset_store.h
)

qt_add_qml_module(sar_gui
  URI Sar.Gui
  VERSION 1.0
  QML_FILES
    qml/Main.qml
)

target_link_libraries(sar_gui PRIVATE
  Qt6::Concurrent
  Qt6::Quick
  Qt6::QuickControls2
  sar_core
)

set_target_properties(sar_gui PROPERTIES
  OUTPUT_NAME "SystemAudioRoute"
)

enable_testing()
add_executable(sar_gui_preset_store_smoke
  preset_store.cpp
  preset_store.h
  tests/preset_store_smoke.cpp
)
target_link_libraries(sar_gui_preset_store_smoke PRIVATE Qt6::Core sar_core)
if(MSVC)
  target_compile_options(sar_gui_preset_store_smoke PRIVATE /UNDEBUG)
else()
  target_compile_options(sar_gui_preset_store_smoke PRIVATE -UNDEBUG)
endif()
add_test(NAME gui_preset_store_smoke COMMAND sar_gui_preset_store_smoke)
set_tests_properties(gui_preset_store_smoke PROPERTIES
  ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_executable(sar_gui_engine_controller_smoke
  engine_controller.cpp
  engine_controller.h
  preset_store.cpp
  preset_store.h
  tests/engine_controller_smoke.cpp
)
target_link_libraries(sar_gui_engine_controller_smoke PRIVATE
  Qt6::Concurrent
  Qt6::Core
  sar_core
)
if(MSVC)
  target_compile_options(sar_gui_engine_controller_smoke PRIVATE /UNDEBUG)
else()
  target_compile_options(sar_gui_engine_controller_smoke PRIVATE -UNDEBUG)
endif()
add_test(NAME gui_engine_controller_smoke COMMAND sar_gui_engine_controller_smoke)
set_tests_properties(gui_engine_controller_smoke PROPERTIES
  ENVIRONMENT_MODIFICATION
    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
  TIMEOUT 10
)
