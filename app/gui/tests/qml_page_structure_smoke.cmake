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

foreach(required_marker
    "objectName: \"routingMatrixMouseArea\""
    "acceptedButtons: Qt.LeftButton | Qt.RightButton"
    "if (mouse.button === Qt.LeftButton)"
    "window.toggleRoute(input.id, output.id)"
    "objectName: \"selectedRouteGainSlider\""
    "window.adjustSelectedRouteGain("
    "property var pendingRouteStates: ({})"
    "property var pendingRouteGains: ({})"
    "WASAPI CAPTURE"
    "ASIO / DAW OUT"
    "WASAPI RENDER"
    "ASIO / DAW IN")
  string(FIND "${qml}" "${required_marker}" marker_position)
  if(marker_position LESS 0)
    message(FATAL_ERROR "Routing matrix QML marker is missing: ${required_marker}")
  endif()
endforeach()

string(FIND "${qml}" "routeSwitch.checked &&" gain_requires_route_toggle)
if(NOT gain_requires_route_toggle LESS 0)
  message(FATAL_ERROR "Selected-route gain must not require toggling the route")
endif()

message(STATUS "QML page structure smoke passed")
