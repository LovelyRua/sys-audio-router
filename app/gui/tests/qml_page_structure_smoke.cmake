if(NOT DEFINED MAIN_QML OR NOT EXISTS "${MAIN_QML}")
  message(FATAL_ERROR "Main QML source was not found: ${MAIN_QML}")
endif()

file(READ "${MAIN_QML}" qml)
string(FIND "${qml}" "objectName: \"audioDevicesPage\"" devices_position)
string(FIND "${qml}" "objectName: \"diagnosticsPage\"" diagnostics_position)
if(devices_position LESS 0 OR diagnostics_position LESS 0 OR
   diagnostics_position LESS_EQUAL devices_position)
  message(FATAL_ERROR "Audio devices and diagnostics page markers are missing or reordered")
endif()

math(EXPR page_span "${diagnostics_position} - ${devices_position}")
string(SUBSTRING "${qml}" ${devices_position} ${page_span} devices_page)
string(REGEX MATCHALL "\\{" devices_open_braces "${devices_page}")
string(REGEX MATCHALL "\\}" devices_close_braces "${devices_page}")
list(LENGTH devices_open_braces devices_open_count)
list(LENGTH devices_close_braces devices_close_count)
if(NOT devices_close_count EQUAL devices_open_count)
  message(FATAL_ERROR
    "Audio devices page must close before diagnostics begins "
    "(opens=${devices_open_count}, closes=${devices_close_count})")
endif()

message(STATUS "QML page structure smoke passed")
