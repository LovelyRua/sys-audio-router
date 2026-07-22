qt_add_executable(sar_gui WIN32
  engine_controller.cpp
  engine_controller.h
  main.cpp
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
