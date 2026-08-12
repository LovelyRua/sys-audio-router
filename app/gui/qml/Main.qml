import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: Math.min(1440, Screen.desktopAvailableWidth)
    height: Math.min(900, Screen.desktopAvailableHeight - 36,
                     Screen.height - 84)
    minimumWidth: 900
    minimumHeight: 640
    visible: true
    title: "System Audio Route"
    color: colors.canvas

    QtObject {
        id: colors
        readonly property color canvas: "#101315"
        readonly property color surface: "#171b1e"
        readonly property color raised: "#20262a"
        readonly property color hover: "#293136"
        readonly property color line: "#323a3f"
        readonly property color text: "#edf1f2"
        readonly property color muted: "#98a3a8"
        readonly property color healthy: "#55d6a5"
        readonly property color cyan: "#5ab8d8"
        readonly property color warning: "#e5b65a"
        readonly property color danger: "#ef6b72"
    }

    property string currentView: "matrix"
    property string selectedInputId: ""
    property string selectedInputLabel: "No input selected"
    property string selectedOutputId: ""
    property string selectedOutputLabel: "No output selected"
    property bool runtimeDraftDirty: false
    property string runtimeDraftMode: "render"
    property string runtimeDraftRenderDeviceId: ""
    property string runtimeDraftCaptureDeviceId: ""
    property var pendingRouteStates: ({})
    property var pendingRouteGains: ({})
    property var routeStateCache: ({})
    property bool showInactiveIo: false
    readonly property var matrixInputs: projectEndpoints(engine.inputs, true,
                                                          engine.runtimeMode,
                                                          showInactiveIo)
    readonly property var matrixOutputs: projectEndpoints(engine.outputs, false,
                                                           engine.runtimeMode,
                                                           showInactiveIo)
    property real meterTarget: 0
    property real meterLevel: 0
    property real meterHold: 0
    property int meterHoldTicks: 0

    function levelToMeterPosition(level) {
        if (level <= 0.001)
            return 0
        var db = 20 * Math.log(level) / Math.LN10
        return Math.max(0, Math.min(1, (db + 60) / 60))
    }

    Timer {
        interval: 16
        repeat: true
        running: true
        onTriggered: {
            var target = Math.max(0, Math.min(1, window.meterTarget))
            if (target >= window.meterLevel)
                window.meterLevel = target
            else
                window.meterLevel = Math.max(target, window.meterLevel * 0.91)
            if (target >= window.meterHold) {
                window.meterHold = target
                window.meterHoldTicks = 75
            } else if (window.meterHoldTicks > 0) {
                --window.meterHoldTicks
            } else {
                window.meterHold = Math.max(window.meterLevel,
                                            window.meterHold * 0.985)
            }
        }
    }

    function routeKey(inputId, outputId) {
        return inputId.length + ":" + inputId + outputId
    }

    function projectEndpoints(endpoints, source, runtimeMode, includeInactive) {
        var projected = []
        for (var index = 0; index < endpoints.length; ++index) {
            var endpoint = endpoints[index]
            var family = endpointFamily(endpoint, index, endpoints.length)
            var active = runtimeMode === "duplex" ||
                    (source ? family !== "wasapi" : family !== "asio")
            if (active || includeInactive) {
                projected.push({
                    id: endpoint.id,
                    label: endpoint.label,
                    active: active
                })
            }
        }
        return projected
    }

    function endpointActive(endpointId) {
        var models = [matrixInputs, matrixOutputs]
        for (var modelIndex = 0; modelIndex < models.length; ++modelIndex) {
            for (var index = 0; index < models[modelIndex].length; ++index) {
                if (models[modelIndex][index].id === endpointId)
                    return models[modelIndex][index].active
            }
        }
        return false
    }

    function endpointFamily(endpoint, index, count) {
        var identity = (String(endpoint.id) + " " + String(endpoint.label)).toLowerCase()
        if (identity.indexOf("asio") >= 0 || identity.indexOf("daw") >= 0 ||
                identity.indexOf("virtual") >= 0)
            return "asio"
        if (identity.indexOf("wasapi") >= 0 || identity.indexOf("capture") >= 0 ||
                identity.indexOf("render") >= 0 || identity.indexOf("monitor") >= 0 ||
                identity.indexOf("hardware") >= 0)
            return "wasapi"
        // The unified duplex layout is conventionally WASAPI L/R followed by ASIO L/R.
        if (count === 4)
            return index < 2 ? "wasapi" : "asio"
        return "other"
    }

    function endpointGroupTitle(endpoint, index, count, source) {
        var family = endpointFamily(endpoint, index, count)
        if (family === "wasapi")
            return source ? "WASAPI CAPTURE" : "WASAPI RENDER"
        if (family === "asio")
            return source ? "ASIO / DAW OUT" : "ASIO / DAW IN"
        return source ? "OTHER SOURCES" : "OTHER DESTINATIONS"
    }

    function endpointGroupColor(endpoint, index, count) {
        var family = endpointFamily(endpoint, index, count)
        return family === "wasapi" ? colors.cyan
             : family === "asio" ? colors.healthy : colors.muted
    }

    function linearToDb(gain) {
        return gain > 0.001 ? 20 * Math.log(gain) / Math.LN10 : -60
    }

    function dbToLinear(db) {
        return db <= -60 ? 0 : Math.pow(10, db / 20)
    }

    function rebuildRouteStateCache() {
        var nextCache = {}
        for (var index = 0; index < engine.routes.length; ++index) {
            var route = engine.routes[index]
            nextCache[routeKey(route.inputId, route.outputId)] = {
                muted: route.muted,
                gain: route.gain
            }
        }
        routeStateCache = nextCache
    }

    function clearRouteSelection() {
        selectedInputId = ""
        selectedInputLabel = "No input selected"
        selectedOutputId = ""
        selectedOutputLabel = "No output selected"
    }

    function validateRouteSelection() {
        var inputLabel = ""
        var outputLabel = ""
        for (var inputIndex = 0; inputIndex < matrixInputs.length; ++inputIndex) {
            if (matrixInputs[inputIndex].id === selectedInputId)
                inputLabel = matrixInputs[inputIndex].label
        }
        for (var outputIndex = 0; outputIndex < matrixOutputs.length; ++outputIndex) {
            if (matrixOutputs[outputIndex].id === selectedOutputId)
                outputLabel = matrixOutputs[outputIndex].label
        }
        if (inputLabel.length === 0 || outputLabel.length === 0) {
            clearRouteSelection()
            return
        }
        selectedInputLabel = inputLabel
        selectedOutputLabel = outputLabel
    }

    function routeDisplayEnabled(inputId, outputId) {
        var pending = pendingRouteStates[routeKey(inputId, outputId)]
        if (pending !== undefined)
            return pending
        var route = routeStateCache[routeKey(inputId, outputId)]
        return route !== undefined && !route.muted
    }

    function routeDisplayState(inputId, outputId) {
        var pending = pendingRouteStates[routeKey(inputId, outputId)]
        if (pending !== undefined)
            return pending ? "pending-on" : "pending-off"
        var route = routeStateCache[routeKey(inputId, outputId)]
        if (route === undefined)
            return "off"
        return route.muted ? "muted" : "on"
    }

    function routeDisplayGain(inputId, outputId) {
        var key = routeKey(inputId, outputId)
        var pending = pendingRouteGains[key]
        if (pending !== undefined)
            return pending
        var route = routeStateCache[key]
        return route !== undefined ? route.gain : 1.0
    }

    function setPendingRouteState(inputId, outputId, enabled) {
        var next = Object.assign({}, pendingRouteStates)
        next[routeKey(inputId, outputId)] = enabled
        pendingRouteStates = next
    }

    function setPendingRouteGain(inputId, outputId, gain) {
        var next = Object.assign({}, pendingRouteGains)
        next[routeKey(inputId, outputId)] = gain
        pendingRouteGains = next
    }

    function clearPendingRouteUpdates() {
        pendingRouteStates = ({})
        pendingRouteGains = ({})
    }

    function selectRoute(inputIndex, outputIndex) {
        if (inputIndex < 0 || inputIndex >= matrixInputs.length ||
                outputIndex < 0 || outputIndex >= matrixOutputs.length)
            return false
        var input = matrixInputs[inputIndex]
        var output = matrixOutputs[outputIndex]
        selectedInputId = input.id
        selectedInputLabel = input.label
        selectedOutputId = output.id
        selectedOutputLabel = output.label
        return true
    }

    function endpointIndex(model, endpointId) {
        for (var index = 0; index < model.length; ++index) {
            if (model[index].id === endpointId)
                return index
        }
        return -1
    }

    function moveRouteSelection(inputDelta, outputDelta) {
        if (matrixInputs.length === 0 || matrixOutputs.length === 0)
            return
        var inputIndex = endpointIndex(matrixInputs, selectedInputId)
        var outputIndex = endpointIndex(matrixOutputs, selectedOutputId)
        if (inputIndex < 0 || outputIndex < 0) {
            inputIndex = 0
            outputIndex = 0
        } else {
            inputIndex = Math.max(0, Math.min(matrixInputs.length - 1,
                                             inputIndex + inputDelta))
            outputIndex = Math.max(0, Math.min(matrixOutputs.length - 1,
                                              outputIndex + outputDelta))
        }
        selectRoute(inputIndex, outputIndex)
        matrixGridFlick.ensureCellVisible(inputIndex, outputIndex)
        matrixGridCanvas.requestPaint()
    }

    function toggleRoute(inputId, outputId) {
        if (!engine.connected || engine.busy || !endpointActive(inputId) ||
                !endpointActive(outputId) || inputId.length === 0 || outputId.length === 0)
            return
        var enabled = !routeDisplayEnabled(inputId, outputId)
        setPendingRouteState(inputId, outputId, enabled)
        engine.setRoute(inputId, outputId, enabled)
    }

    function toggleSelectedRoute() {
        toggleRoute(selectedInputId, selectedOutputId)
    }

    function adjustSelectedRouteGain(linearGain) {
        if (!engine.connected || !endpointActive(selectedInputId) ||
                !endpointActive(selectedOutputId) || selectedInputId.length === 0 ||
                selectedOutputId.length === 0)
            return
        setPendingRouteGain(selectedInputId, selectedOutputId, linearGain)
        engine.setRouteGain(selectedInputId, selectedOutputId, linearGain)
    }

    function syncRuntimeDraft() {
        var engineMode = engine.runtimeMode === "duplex" ? "duplex" : "render"
        var matchesDraft = engineMode === runtimeDraftMode
                && engine.runtimeRenderDeviceId === runtimeDraftRenderDeviceId
                && (engineMode !== "duplex"
                    || engine.runtimeCaptureDeviceId === runtimeDraftCaptureDeviceId)
        if (runtimeDraftDirty && !matchesDraft)
            return
        runtimeDraftDirty = false
        runtimeDraftMode = engineMode
        runtimeDraftRenderDeviceId = engine.runtimeRenderDeviceId
        runtimeDraftCaptureDeviceId = engine.runtimeCaptureDeviceId
    }

    function indexForDevice(model, deviceId) {
        for (var index = 0; index < model.length; ++index) {
            if (model[index].id === deviceId)
                return index
        }
        if (deviceId.length !== 0 || model.length === 0)
            return -1
        for (var defaultIndex = 0; defaultIndex < model.length; ++defaultIndex) {
            if (model[defaultIndex].isDefault)
                return defaultIndex
        }
        return 0
    }

    Connections {
        target: engine
        function onDiagnosticsChanged() {
            window.meterTarget = engine.peak
        }
        function onRuntimeChanged() { window.syncRuntimeDraft() }
        function onSessionChanged() {
            window.clearPendingRouteUpdates()
            window.rebuildRouteStateCache()
            window.validateRouteSelection()
            matrixInputHeader.requestPaint()
            matrixOutputHeader.requestPaint()
            matrixGridCanvas.requestPaint()
        }
        function onBusyChanged() {
            if (!engine.busy)
                window.clearPendingRouteUpdates()
            matrixGridCanvas.requestPaint()
        }
        function onConnectionChanged() { matrixGridCanvas.requestPaint() }
    }

    onSelectedInputIdChanged: matrixGridCanvas.requestPaint()
    onSelectedOutputIdChanged: matrixGridCanvas.requestPaint()
    onPendingRouteStatesChanged: matrixGridCanvas.requestPaint()

    Component.onCompleted: {
        syncRuntimeDraft()
        rebuildRouteStateCache()
    }

    Shortcut {
        sequences: ["Ctrl+Z"]
        enabled: window.currentView === "matrix" && engine.canUndo && !engine.busy
        onActivated: engine.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Y", "Ctrl+Shift+Z"]
        enabled: window.currentView === "matrix" && engine.canRedo && !engine.busy
        onActivated: engine.redo()
    }

    component FlatButton: Button {
        id: control
        implicitHeight: 34
        leftPadding: 12
        rightPadding: 12
        font.pixelSize: 12
        font.weight: Font.DemiBold
        contentItem: Text {
            text: control.text
            color: control.enabled ? colors.text : colors.muted
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 4
            color: control.down ? colors.hover
                                : control.hovered ? colors.raised : "transparent"
            border.color: control.highlighted ? colors.cyan : colors.line
        }
    }

    component IconButton: Button {
        id: control
        required property string tooltipText
        implicitWidth: 34
        implicitHeight: 34
        padding: 0
        font.family: "Segoe UI Symbol"
        font.pixelSize: 17
        contentItem: Text {
            text: control.text
            color: control.enabled ? colors.text : colors.muted
            font: control.font
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: control.down ? colors.hover
                                : control.hovered ? colors.raised : "transparent"
            border.color: colors.line
        }
        ToolTip.visible: hovered
        ToolTip.delay: 450
        ToolTip.text: tooltipText
    }

    component ConsoleField: TextField {
        id: control
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 10
        color: colors.text
        selectionColor: colors.cyan
        selectedTextColor: colors.canvas
        placeholderTextColor: colors.muted
        font.pixelSize: 12
        background: Rectangle {
            radius: 3
            color: colors.canvas
            border.color: control.activeFocus ? colors.cyan : colors.line
        }
    }

    component ConsoleCombo: ComboBox {
        id: control
        property string emptyText: "No options"
        function optionText(value) {
            if (value === undefined || value === null)
                return ""
            if (control.textRole.length > 0 && typeof value === "object") {
                var roleValue = value[control.textRole]
                return roleValue === undefined || roleValue === null
                        ? "" : String(roleValue)
            }
            return String(value)
        }
        readonly property string selectedText:
            currentIndex >= 0 && currentIndex < model.length
            ? optionText(model[currentIndex]) : ""
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 28
        font.pixelSize: 12
        contentItem: Text {
            text: control.selectedText.length > 0 ? control.selectedText
                                                  : control.emptyText
            color: control.selectedText.length > 0 ? colors.text : colors.muted
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Text {
            x: control.width - width - 10
            anchors.verticalCenter: parent.verticalCenter
            text: "v"
            color: colors.muted
            font.pixelSize: 10
        }
        background: Rectangle {
            radius: 3
            color: control.hovered ? colors.raised : colors.canvas
            border.color: control.activeFocus ? colors.cyan : colors.line
        }
        delegate: ItemDelegate {
            required property var modelData
            width: control.width
            height: 34
            leftPadding: 10
            contentItem: Text {
                text: control.optionText(modelData)
                color: colors.text
                font.pixelSize: 12
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                color: hovered ? colors.hover : colors.surface
            }
        }
        popup: Popup {
            y: control.height + 2
            width: control.width
            implicitHeight: Math.min(contentItem.implicitHeight, 210)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                color: colors.surface
                border.color: colors.line
                radius: 3
            }
        }
    }

    component NavButton: Button {
        id: control
        required property string viewId
        implicitHeight: 42
        flat: true
        leftPadding: 16
        contentItem: Text {
            text: control.text
            color: currentView === control.viewId ? colors.text : colors.muted
            font.pixelSize: 13
            font.weight: currentView === control.viewId ? Font.DemiBold : Font.Normal
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 3
            color: currentView === control.viewId ? colors.raised
                                                  : control.hovered ? "#1b2023" : "transparent"
            Rectangle {
                width: 3
                height: parent.height - 12
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                color: colors.cyan
                visible: currentView === control.viewId
            }
        }
        onClicked: currentView = viewId
    }

    header: Rectangle {
        height: 58
        color: colors.surface
        border.color: colors.line

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: window.width < 1100 ? 14 : 20
            anchors.rightMargin: window.width < 1100 ? 10 : 16
            spacing: window.width < 1100 ? 10 : 18

            Text {
                text: "SYSTEM AUDIO ROUTE"
                color: colors.text
                font.pixelSize: window.width < 1100 ? 13 : 15
                font.weight: Font.Bold
                Layout.preferredWidth: window.width < 1100 ? 164 : 206
            }

            Rectangle { width: 1; height: 26; color: colors.line }

            RowLayout {
                spacing: 8
                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: engine.connected ? colors.healthy : colors.danger
                }
                Text {
                    text: engine.connectionLabel
                    color: colors.text
                    font.pixelSize: 12
                }
                Text {
                    visible: engine.lastError.length > 0 && !engine.connected
                    text: engine.lastError
                    color: colors.danger
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.maximumWidth: 280
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: engine.sampleRate > 0 ? engine.sampleRate + " Hz" : "-- Hz"
                color: colors.muted
                font.pixelSize: 12
                visible: window.width >= 980
            }
            Text {
                text: engine.blockSize > 0 ? engine.blockSize + " samples" : "-- samples"
                color: colors.muted
                font.pixelSize: 12
                visible: window.width >= 1060
            }
            Text {
                text: "XRUN " + engine.xrunCount
                color: engine.xrunCount > 0 ? colors.warning : colors.muted
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }

            FlatButton {
                objectName: "runtimeActionButton"
                text: !engine.runtimeConfigured ? "Configure audio"
                      : window.width < 1100
                        ? (engine.runtimeRunning ? "Stop" : "Start")
                        : (engine.runtimeRunning ? "Stop engine" : "Start engine")
                highlighted: !engine.runtimeRunning
                enabled: engine.connected && !engine.busy
                onClicked: {
                    if (!engine.runtimeConfigured) {
                        window.currentView = "devices"
                        return
                    }
                    engine.runtimeRunning ? engine.stopRuntime()
                                          : engine.startRuntime()
                }
            }
        }
    }

    footer: Rectangle {
        readonly property string message: engine.lastError.length > 0
                                          ? engine.lastError
                                          : engine.statusMessage
        height: message.length > 0 ? 38 : 0
        visible: height > 0
        color: engine.lastError.length > 0 ? "#321d20" : "#153028"
        border.color: engine.lastError.length > 0 ? colors.danger : colors.healthy
        clip: true

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 10
            spacing: 10
            Text {
                text: engine.lastError.length > 0 ? "ACTION FAILED" : "PRESET"
                color: engine.lastError.length > 0 ? colors.danger : colors.healthy
                font.pixelSize: 10
                font.weight: Font.Bold
            }
            Text {
                text: parent.parent.message
                color: colors.text
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            FlatButton {
                text: "Dismiss"
                onClicked: engine.clearFeedback()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.preferredWidth: window.width < 1150 ? 176 : 218
            Layout.fillHeight: true
            color: colors.surface
            border.color: colors.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 3

                Text {
                    text: "WORKSPACE"
                    color: colors.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    leftPadding: 8
                    topPadding: 10
                    bottomPadding: 6
                }
                NavButton { text: "Routing matrix"; viewId: "matrix"; Layout.fillWidth: true }
                NavButton { text: "Audio devices"; viewId: "devices"; Layout.fillWidth: true }
                NavButton { text: "Diagnostics"; viewId: "diagnostics"; Layout.fillWidth: true }

                Text {
                    text: "PRESETS"
                    color: colors.muted
                    font.pixelSize: 10
                    font.weight: Font.DemiBold
                    leftPadding: 8
                    topPadding: 16
                    bottomPadding: 3
                }
                ConsoleCombo {
                    id: presetBrowser
                    Layout.fillWidth: true
                    model: engine.presetNames
                    emptyText: "No saved presets"
                    currentIndex: engine.activePresetName.length > 0
                                  ? engine.presetNames.indexOf(engine.activePresetName)
                                  : -1
                    enabled: model.length > 0 && !engine.busy
                    onActivated: presetName.text = currentText
                }
                ConsoleField {
                    id: presetName
                    Layout.fillWidth: true
                    placeholderText: "Preset name"
                    text: engine.activePresetName
                    maximumLength: 80
                    selectByMouse: true
                    onAccepted: {
                        if (engine.connected && text.trim().length > 0 && !engine.busy)
                            engine.savePreset(text)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    FlatButton {
                        text: "Load"
                        Layout.fillWidth: true
                        enabled: engine.connected &&
                                 presetBrowser.currentIndex >= 0 &&
                                 !engine.busy
                        onClicked: engine.loadPreset(presetBrowser.currentText)
                    }
                    FlatButton {
                        text: "Save"
                        highlighted: true
                        Layout.fillWidth: true
                        enabled: engine.connected &&
                                 presetName.text.trim().length > 0 &&
                                 !engine.busy
                        onClicked: engine.savePreset(presetName.text)
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 8
                    spacing: 5
                    Text {
                        text: "GRAPH " + engine.graphVersion
                        color: colors.muted
                        font.pixelSize: 10
                    }
                    Text {
                        text: engine.activeClients + " ASIO  /  " +
                              engine.droppedBlocks + " dropped"
                        color: engine.droppedBlocks > 0 ? colors.danger : colors.muted
                        font.pixelSize: 11
                    }
                }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentView === "matrix" ? 0
                         : currentView === "devices" ? 1 : 2

            RowLayout {
                spacing: 0

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: colors.canvas

                    ColumnLayout {
                        anchors.fill: parent
                        spacing: 0

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 52
                            color: colors.canvas
                            border.color: colors.line
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 18
                                anchors.rightMargin: 12
                                Text {
                                    text: "Routing matrix"
                                    color: colors.text
                                    font.pixelSize: 16
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    text: matrixInputs.length + " inputs  /  " +
                                          matrixOutputs.length + " outputs"
                                    color: colors.muted
                                    font.pixelSize: 11
                                }
                                Text {
                                    visible: !engine.connected || engine.busy
                                    text: !engine.connected ? "OFFLINE" : "APPLYING"
                                    color: !engine.connected ? colors.danger : colors.warning
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                CheckBox {
                                    objectName: "showInactiveIoCheckBox"
                                    text: "Show inactive I/O"
                                    checked: showInactiveIo
                                    onToggled: {
                                        showInactiveIo = checked
                                        validateRouteSelection()
                                        matrixInputHeader.requestPaint()
                                        matrixOutputHeader.requestPaint()
                                        matrixGridCanvas.requestPaint()
                                    }
                                }
                                IconButton {
                                    text: "↶"
                                    tooltipText: "Undo route edit (Ctrl+Z)"
                                    enabled: engine.canUndo && !engine.busy
                                    onClicked: engine.undo()
                                }
                                IconButton {
                                    text: "↷"
                                    tooltipText: "Redo route edit (Ctrl+Y or Ctrl+Shift+Z)"
                                    enabled: engine.canRedo && !engine.busy
                                    onClicked: engine.redo()
                                }
                                FlatButton {
                                    text: "Refresh"
                                    enabled: !engine.busy
                                    onClicked: engine.refresh()
                                }
                            }
                        }

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Text {
                                anchors.centerIn: parent
                                visible: matrixInputs.length === 0 || matrixOutputs.length === 0
                                text: engine.connected ? "No routable endpoints in this preset"
                                                       : "Waiting for the engine"
                                color: colors.muted
                                font.pixelSize: 13
                            }

                            Item {
                                id: matrixViewport
                                anchors.fill: parent
                                anchors.margins: 18
                                visible: matrixInputs.length > 0 && matrixOutputs.length > 0
                                clip: true
                                property int labelWidth: window.width < 1100 ? 154 : 190
                                property int headerHeight: 96
                                property int cellSize: 44
                                property int groupLabelWidth: window.width < 1100 ? 44 : 56

                                function elideCanvasText(context, value, maximumWidth) {
                                    var label = String(value)
                                    if (context.measureText(label).width <= maximumWidth)
                                        return label
                                    var suffix = "..."
                                    while (label.length > 1 &&
                                           context.measureText(label + suffix).width > maximumWidth)
                                        label = label.slice(0, -1)
                                    return label + suffix
                                }

                                Rectangle {
                                    width: matrixViewport.labelWidth
                                    height: matrixViewport.headerHeight
                                    color: colors.surface
                                    border.color: colors.line
                                    Text {
                                        anchors.centerIn: parent
                                        text: "DESTINATION  /  SOURCE"
                                        color: colors.muted
                                        font.pixelSize: 9
                                        font.weight: Font.DemiBold
                                    }
                                }

                                Canvas {
                                    id: matrixInputHeader
                                    anchors.left: parent.left
                                    anchors.leftMargin: matrixViewport.labelWidth
                                    anchors.right: parent.right
                                    height: matrixViewport.headerHeight
                                    onPaint: {
                                        var context = getContext("2d")
                                        context.clearRect(0, 0, width, height)
                                        context.font = "10px Segoe UI"
                                        var cellSize = matrixViewport.cellSize
                                        var firstColumn = Math.floor(matrixGridFlick.contentX / cellSize)
                                        var x = firstColumn * cellSize - matrixGridFlick.contentX
                                        for (var column = firstColumn;
                                             column < matrixInputs.length && x < width;
                                             ++column, x += cellSize) {
                                            var endpoint = matrixInputs[column]
                                            var group = window.endpointGroupTitle(
                                                        endpoint, column,
                                                        matrixInputs.length, true)
                                            var groupStart = column === 0 || group !== window.endpointGroupTitle(
                                                        matrixInputs[column - 1], column - 1,
                                                        matrixInputs.length, true)
                                            if (groupStart) {
                                                var groupEnd = column + 1
                                                while (groupEnd < matrixInputs.length &&
                                                       window.endpointGroupTitle(
                                                           matrixInputs[groupEnd], groupEnd,
                                                           matrixInputs.length, true) === group)
                                                    ++groupEnd
                                                var groupWidth = (groupEnd - column) * cellSize
                                                context.fillStyle = "#20262a"
                                                context.fillRect(x + 1, 1, groupWidth - 3, 20)
                                                context.fillStyle = window.endpointGroupColor(
                                                                    endpoint, column,
                                                                    matrixInputs.length)
                                                context.fillRect(x + 1, 20, groupWidth - 3, 2)
                                                context.font = "bold 9px Segoe UI"
                                                context.textAlign = "center"
                                                context.fillText(group, x + groupWidth / 2, 14)
                                            }
                                            context.save()
                                            context.font = "10px Segoe UI"
                                            context.fillStyle = endpoint.active ? colors.muted : "#596166"
                                            context.textAlign = "left"
                                            context.translate(x + cellSize / 2, height - 7)
                                            context.rotate(-Math.PI / 3)
                                            var label = matrixViewport.elideCanvasText(
                                                        context, endpoint.label, 86)
                                            context.fillText(label, 0, 0)
                                            context.restore()
                                        }
                                    }
                                }

                                Canvas {
                                    id: matrixOutputHeader
                                    width: matrixViewport.labelWidth
                                    anchors.top: parent.top
                                    anchors.topMargin: matrixViewport.headerHeight
                                    anchors.bottom: parent.bottom
                                    onPaint: {
                                        var context = getContext("2d")
                                        context.clearRect(0, 0, width, height)
                                        context.font = "11px Segoe UI"
                                        var cellSize = matrixViewport.cellSize
                                        var firstRow = Math.floor(matrixGridFlick.contentY / cellSize)
                                        var y = firstRow * cellSize - matrixGridFlick.contentY
                                        for (var row = firstRow;
                                             row < matrixOutputs.length && y < height;
                                             ++row, y += cellSize) {
                                            var endpoint = matrixOutputs[row]
                                            var groupTitle = window.endpointGroupTitle(
                                                                 endpoint, row,
                                                                 matrixOutputs.length, false)
                                            var groupStart = row === 0 ||
                                                    groupTitle !== window.endpointGroupTitle(
                                                        matrixOutputs[row - 1], row - 1,
                                                        matrixOutputs.length, false)
                                            context.fillStyle = colors.surface
                                            context.fillRect(0, y + 1, width - 3, cellSize - 3)
                                            context.strokeStyle = colors.line
                                            context.strokeRect(0.5, y + 1.5, width - 4, cellSize - 4)
                                            if (groupStart) {
                                                var groupEnd = row + 1
                                                while (groupEnd < matrixOutputs.length &&
                                                       window.endpointGroupTitle(
                                                           matrixOutputs[groupEnd], groupEnd,
                                                           matrixOutputs.length, false) === groupTitle)
                                                    ++groupEnd
                                                var groupHeight = (groupEnd - row) * cellSize
                                                context.fillStyle = "#20262a"
                                                context.fillRect(1, y + 1,
                                                                 matrixViewport.groupLabelWidth - 3,
                                                                 groupHeight - 3)
                                                context.fillStyle = window.endpointGroupColor(
                                                                    endpoint, row,
                                                                    matrixOutputs.length)
                                                context.fillRect(matrixViewport.groupLabelWidth - 3,
                                                                 y + 1, 2,
                                                                 groupHeight - 3)
                                                context.fillStyle = window.endpointGroupColor(
                                                                    endpoint, row,
                                                                    matrixOutputs.length)
                                                context.font = "bold 8px Segoe UI"
                                                context.textAlign = "center"
                                                context.textBaseline = "middle"
                                                var family = window.endpointFamily(
                                                                 endpoint, row,
                                                                 matrixOutputs.length)
                                                var centerY = y + groupHeight / 2
                                                context.fillText(family === "wasapi" ? "WASAPI"
                                                                 : family === "asio" ? "ASIO" : "OTHER",
                                                                 matrixViewport.groupLabelWidth / 2,
                                                                 centerY - 6)
                                                context.fillText(family === "wasapi" ? "RENDER"
                                                                 : family === "asio" ? "DAW IN" : "DEST",
                                                                 matrixViewport.groupLabelWidth / 2,
                                                                 centerY + 7)
                                            }
                                            context.fillStyle = endpoint.active ? colors.text : "#596166"
                                            context.textBaseline = "middle"
                                            context.font = "11px Segoe UI"
                                            context.textAlign = "left"
                                            var label = matrixViewport.elideCanvasText(
                                                        context, endpoint.label,
                                                        width - matrixViewport.groupLabelWidth - 14)
                                            context.fillText(label,
                                                             matrixViewport.groupLabelWidth + 7,
                                                             y + cellSize / 2)
                                        }
                                    }
                                }

                                Flickable {
                                    id: matrixGridFlick
                                    anchors.left: parent.left
                                    anchors.leftMargin: matrixViewport.labelWidth
                                    anchors.top: parent.top
                                    anchors.topMargin: matrixViewport.headerHeight
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    contentWidth: Math.max(width,
                                                           matrixInputs.length * matrixViewport.cellSize)
                                    contentHeight: Math.max(height,
                                                            matrixOutputs.length * matrixViewport.cellSize)
                                    boundsBehavior: Flickable.StopAtBounds
                                    clip: true
                                    focus: true

                                    function ensureCellVisible(inputIndex, outputIndex) {
                                        var left = inputIndex * matrixViewport.cellSize
                                        var top = outputIndex * matrixViewport.cellSize
                                        if (left < contentX)
                                            contentX = left
                                        else if (left + matrixViewport.cellSize > contentX + width)
                                            contentX = left + matrixViewport.cellSize - width
                                        if (top < contentY)
                                            contentY = top
                                        else if (top + matrixViewport.cellSize > contentY + height)
                                            contentY = top + matrixViewport.cellSize - height
                                    }

                                    onContentXChanged: {
                                        matrixInputHeader.requestPaint()
                                        matrixGridCanvas.requestPaint()
                                    }
                                    onContentYChanged: {
                                        matrixOutputHeader.requestPaint()
                                        matrixGridCanvas.requestPaint()
                                    }
                                    onWidthChanged: matrixGridCanvas.requestPaint()
                                    onHeightChanged: matrixGridCanvas.requestPaint()

                                    Keys.onPressed: function(event) {
                                        if (event.key === Qt.Key_Left)
                                            window.moveRouteSelection(-1, 0)
                                        else if (event.key === Qt.Key_Right)
                                            window.moveRouteSelection(1, 0)
                                        else if (event.key === Qt.Key_Up)
                                            window.moveRouteSelection(0, -1)
                                        else if (event.key === Qt.Key_Down)
                                            window.moveRouteSelection(0, 1)
                                        else if (event.key === Qt.Key_Space ||
                                                 event.key === Qt.Key_Return ||
                                                 event.key === Qt.Key_Enter)
                                            window.toggleSelectedRoute()
                                        else
                                            return
                                        event.accepted = true
                                    }

                                    Item {
                                        width: matrixGridFlick.contentWidth
                                        height: matrixGridFlick.contentHeight
                                        MouseArea {
                                            id: matrixMouse
                                            objectName: "routingMatrixMouseArea"
                                            anchors.fill: parent
                                            enabled: engine.connected
                                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                                            hoverEnabled: true
                                            preventStealing: false
                                            property int hoverInput: -1
                                            property int hoverOutput: -1
                                            onPositionChanged: function(mouse) {
                                                hoverInput = Math.floor(mouse.x / matrixViewport.cellSize)
                                                hoverOutput = Math.floor(mouse.y / matrixViewport.cellSize)
                                                matrixGridCanvas.requestPaint()
                                            }
                                            onExited: {
                                                hoverInput = -1
                                                hoverOutput = -1
                                                matrixGridCanvas.requestPaint()
                                            }
                                            onClicked: function(mouse) {
                                                var inputIndex = Math.floor(mouse.x /
                                                                          matrixViewport.cellSize)
                                                var outputIndex = Math.floor(mouse.y /
                                                                            matrixViewport.cellSize)
                                                if (!window.selectRoute(inputIndex, outputIndex))
                                                    return
                                                matrixGridFlick.forceActiveFocus()
                                                if (mouse.button === Qt.LeftButton) {
                                                    var input = matrixInputs[inputIndex]
                                                    var output = matrixOutputs[outputIndex]
                                                    window.toggleRoute(input.id, output.id)
                                                }
                                            }
                                        }
                                    }

                                    ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                                }

                                Canvas {
                                    id: matrixGridCanvas
                                    anchors.fill: matrixGridFlick
                                    z: -1
                                    onPaint: {
                                        var context = getContext("2d")
                                        context.clearRect(0, 0, width, height)
                                        var cellSize = matrixViewport.cellSize
                                        var firstColumn = Math.floor(matrixGridFlick.contentX / cellSize)
                                        var firstRow = Math.floor(matrixGridFlick.contentY / cellSize)
                                        var startX = firstColumn * cellSize - matrixGridFlick.contentX
                                        var startY = firstRow * cellSize - matrixGridFlick.contentY
                                        for (var row = firstRow, y = startY;
                                             row < matrixOutputs.length && y < height;
                                             ++row, y += cellSize) {
                                            for (var column = firstColumn, x = startX;
                                                 column < matrixInputs.length && x < width;
                                                 ++column, x += cellSize) {
                                                    var input = matrixInputs[column]
                                                    var output = matrixOutputs[row]
                                                    var inputId = input.id
                                                    var outputId = output.id
                                                    var active = input.active && output.active
                                                    var state = window.routeDisplayState(inputId, outputId)
                                                var selected = selectedInputId === inputId &&
                                                               selectedOutputId === outputId
                                                var hovered = matrixMouse.hoverInput === column &&
                                                              matrixMouse.hoverOutput === row
                                                var routeOn = state === "on" || state === "pending-on"
                                                var inputGroupStart = column > 0 &&
                                                        window.endpointFamily(matrixInputs[column], column,
                                                                              matrixInputs.length) !==
                                                        window.endpointFamily(matrixInputs[column - 1], column - 1,
                                                                              matrixInputs.length)
                                                var outputGroupStart = row > 0 &&
                                                        window.endpointFamily(matrixOutputs[row], row,
                                                                              matrixOutputs.length) !==
                                                        window.endpointFamily(matrixOutputs[row - 1], row - 1,
                                                                              matrixOutputs.length)
                                                context.fillStyle = !active ? "#15191b"
                                                                  : routeOn ? "#245a49"
                                                                  : state === "muted" ? "#4a3b20"
                                                                  : hovered && engine.connected && !engine.busy
                                                                    ? colors.hover : colors.surface
                                                context.fillRect(x + 1, y + 1,
                                                                 cellSize - 3, cellSize - 3)
                                                context.lineWidth = selected ? 2 : 1
                                                context.strokeStyle = !active ? "#252b2e"
                                                                    : selected ? colors.cyan
                                                                    : routeOn ? colors.healthy
                                                                    : state === "muted" ? colors.warning
                                                                    : colors.line
                                                context.strokeRect(x + (selected ? 1 : 1.5),
                                                                   y + (selected ? 1 : 1.5),
                                                                   cellSize - (selected ? 3 : 4),
                                                                   cellSize - (selected ? 3 : 4))
                                                context.fillStyle = !active ? "#30373a"
                                                                  : routeOn ? colors.healthy
                                                                  : state === "muted" ? colors.warning
                                                                  : colors.line
                                                context.beginPath()
                                                context.arc(x + cellSize / 2,
                                                            y + cellSize / 2,
                                                            4, 0, Math.PI * 2)
                                                context.fill()
                                                if (state === "muted") {
                                                    context.strokeStyle = colors.warning
                                                    context.lineWidth = 2
                                                    context.beginPath()
                                                    context.moveTo(x + cellSize / 2 - 7,
                                                                   y + cellSize / 2 + 7)
                                                    context.lineTo(x + cellSize / 2 + 7,
                                                                   y + cellSize / 2 - 7)
                                                    context.stroke()
                                                }
                                                if (state === "pending-on" ||
                                                        state === "pending-off") {
                                                    context.strokeStyle = colors.cyan
                                                    context.lineWidth = 2
                                                    context.strokeRect(x + 4, y + 4,
                                                                       cellSize - 9, cellSize - 9)
                                                }
                                                if (inputGroupStart) {
                                                    context.fillStyle = colors.cyan
                                                    context.fillRect(x - 1, y + 1, 2,
                                                                     cellSize - 3)
                                                }
                                                if (outputGroupStart) {
                                                    context.fillStyle = colors.healthy
                                                    context.fillRect(x + 1, y - 1,
                                                                     cellSize - 3, 2)
                                                }
                                                if (!engine.connected) {
                                                    context.fillStyle = "#99101315"
                                                    context.fillRect(x + 1, y + 1,
                                                                     cellSize - 3, cellSize - 3)
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.margins: 8
                                    width: matrixStateText.implicitWidth + 16
                                    height: 26
                                    radius: 3
                                    visible: !engine.connected || engine.busy
                                    color: colors.raised
                                    border.color: !engine.connected ? colors.danger : colors.warning
                                    Text {
                                        id: matrixStateText
                                        anchors.centerIn: parent
                                        text: !engine.connected ? "Matrix unavailable" : "Applying change..."
                                        color: !engine.connected ? colors.danger : colors.warning
                                        font.pixelSize: 10
                                        font.weight: Font.DemiBold
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.preferredWidth: window.width < 1150 ? 224 : 286
                    Layout.minimumWidth: 210
                    Layout.fillHeight: true
                    color: colors.surface
                    border.color: colors.line

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: window.width < 1150 ? 12 : 18
                        spacing: window.width < 1150 ? 11 : 14

                        Text {
                            text: "ROUTE INSPECTOR"
                            color: colors.muted
                            font.pixelSize: 10
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: selectedInputLabel
                            color: colors.text
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: "to  " + selectedOutputLabel
                            color: colors.muted
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }

                        Switch {
                            id: routeSwitch
                            objectName: "routeEnabledSwitch"
                            text: "Route enabled"
                            enabled: selectedInputId.length > 0 &&
                                     window.endpointActive(selectedInputId) &&
                                     window.endpointActive(selectedOutputId) &&
                                     engine.connected && !engine.busy
                            checked: {
                                engine.routeRevision;
                                return selectedInputId.length > 0 &&
                                       window.routeDisplayEnabled(selectedInputId,
                                                                  selectedOutputId)
                            }
                            onClicked: {
                                window.setPendingRouteState(selectedInputId,
                                                            selectedOutputId,
                                                            checked)
                                engine.setRoute(selectedInputId, selectedOutputId, checked)
                            }
                            contentItem: Text {
                                text: routeSwitch.text
                                color: routeSwitch.enabled ? colors.text : colors.muted
                                font.pixelSize: 12
                                leftPadding: routeSwitch.indicator.width + 10
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Text { text: "Gain"; color: colors.muted; font.pixelSize: 11 }
                        RowLayout {
                            Layout.fillWidth: true
                            Slider {
                                id: gainSlider
                                objectName: "selectedRouteGainSlider"
                                Layout.fillWidth: true
                                from: -60
                                to: 12
                                stepSize: 0.1
                                enabled: selectedInputId.length > 0 &&
                                         window.endpointActive(selectedInputId) &&
                                         window.endpointActive(selectedOutputId) &&
                                         engine.connected
                                value: {
                                    engine.routeRevision;
                                    return selectedInputId.length > 0
                                           ? window.linearToDb(
                                                 window.routeDisplayGain(selectedInputId,
                                                                         selectedOutputId)) : 0
                                }
                                onMoved: window.adjustSelectedRouteGain(
                                             window.dbToLinear(value))
                                onPressedChanged: {
                                    if (!pressed) {
                                        window.adjustSelectedRouteGain(
                                                    window.dbToLinear(value))
                                    }
                                }
                            }
                            Text {
                                text: gainSlider.value <= -60
                                      ? "-inf"
                                      : (gainSlider.value > 0 ? "+" : "") +
                                        gainSlider.value.toFixed(1) + " dB"
                                color: colors.text
                                font.family: "Consolas"
                                font.pixelSize: 11
                                Layout.preferredWidth: 66
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                        Text { text: "ASIO BUS LEVEL"; color: colors.muted; font.pixelSize: 10 }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 16
                            radius: 2
                            color: colors.canvas
                            border.color: colors.line
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 2
                                width: Math.max(0, (parent.width - 4) *
                                                window.levelToMeterPosition(window.meterLevel))
                                height: parent.height
                                radius: 2
                                color: window.meterLevel > 0.95 ? colors.danger
                                      : window.meterLevel > 0.50 ? colors.warning : colors.healthy
                            }
                            Rectangle {
                                visible: window.meterHold > 0.001
                                width: 2
                                height: parent.height - 2
                                y: 1
                                x: Math.max(1, Math.min(parent.width - width - 1,
                                    (parent.width - 4) *
                                    window.levelToMeterPosition(window.meterHold) + 2))
                                color: window.meterHold > 0.95 ? colors.danger : colors.text
                            }
                        }
                        Text {
                            text: window.meterLevel > 0.001
                                  ? (20 * Math.log(window.meterLevel) / Math.LN10).toFixed(1) + " dBFS"
                                                  : "-inf dBFS"
                            color: colors.text
                            font.family: "Consolas"
                            font.pixelSize: 11
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            Rectangle {
                objectName: "audioDevicesPage"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: colors.canvas
                ScrollView {
                    id: devicesScroll
                    anchors.fill: parent
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: devicesContent.implicitHeight + 48
                ColumnLayout {
                    id: devicesContent
                    x: 24
                    y: 24
                    width: Math.max(0, devicesScroll.availableWidth - 48)
                    spacing: 14
                    Text { text: "Audio devices"; color: colors.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: engine.devices.length + " endpoints reported by the engine"; color: colors.muted; font.pixelSize: 12 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                    Rectangle {
                        Layout.fillWidth: true
                        color: colors.surface
                        border.color: colors.line
                        radius: 4
                        implicitHeight: runtimeConfigColumn.implicitHeight + 28

                        ColumnLayout {
                            id: runtimeConfigColumn
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 10

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: "WASAPI runtime"
                                    color: colors.text
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }
                                Item { Layout.fillWidth: true }
                                Text {
                                    text: engine.runtimeConfigured ?
                                              (engine.runtimeMode === "duplex" ? "DUPLEX" : "RENDER") :
                                          "NOT CONFIGURED"
                                    color: engine.runtimeConfigured ? colors.healthy : colors.warning
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "Mode"; color: colors.muted; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                ConsoleCombo {
                                    id: runtimeModeCombo
                                    objectName: "runtimeModeCombo"
                                    Layout.fillWidth: true
                                    model: ["WASAPI render", "WASAPI duplex"]
                                    currentIndex: window.runtimeDraftMode === "duplex" ? 1 : 0
                                    onActivated: function(index) {
                                        window.runtimeDraftMode = index === 1 ? "duplex" : "render"
                                        window.runtimeDraftDirty = true
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: "Render"; color: colors.muted; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                ConsoleCombo {
                                    id: renderDeviceCombo
                                    objectName: "renderDeviceCombo"
                                    Layout.fillWidth: true
                                    textRole: "label"
                                    emptyText: "No render devices"
                                    model: engine.devices.filter(function (device) {
                                        return device.isWasapi &&
                                               (device.direction === 1 || device.direction === 2)
                                    })
                                    currentIndex: window.indexForDevice(
                                                      renderDeviceCombo.model,
                                                      window.runtimeDraftRenderDeviceId)
                                    onActivated: function(index) {
                                        if (index >= 0) {
                                            window.runtimeDraftRenderDeviceId = model[index].id
                                            window.runtimeDraftDirty = true
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                visible: window.runtimeDraftMode === "duplex"
                                Text { text: "Capture"; color: colors.muted; font.pixelSize: 11; Layout.preferredWidth: 72 }
                                ConsoleCombo {
                                    id: captureDeviceCombo
                                    objectName: "captureDeviceCombo"
                                    Layout.fillWidth: true
                                    textRole: "label"
                                    emptyText: "No capture devices"
                                    model: engine.devices.filter(function (device) {
                                        return device.isWasapi &&
                                               (device.direction === 0 || device.direction === 2)
                                    })
                                    currentIndex: window.indexForDevice(
                                                      captureDeviceCombo.model,
                                                      window.runtimeDraftCaptureDeviceId)
                                    onActivated: function(index) {
                                        if (index >= 0) {
                                            window.runtimeDraftCaptureDeviceId = model[index].id
                                            window.runtimeDraftDirty = true
                                        }
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text {
                                    text: engine.runtimeRunning
                                          ? "Apply restarts the engine with the selected devices"
                                          : "Select devices, then apply the runtime"
                                    color: colors.muted
                                    font.pixelSize: 10
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                FlatButton {
                                    objectName: "applyRuntimeButton"
                                    text: "Apply"
                                    highlighted: true
                                    enabled: engine.connected && !engine.busy &&
                                             renderDeviceCombo.currentIndex >= 0 &&
                                            (window.runtimeDraftMode === "render" || captureDeviceCombo.currentIndex >= 0)
                                    onClicked: {
                                        var renderDevice = renderDeviceCombo.model[renderDeviceCombo.currentIndex]
                                        var captureDevice = window.runtimeDraftMode === "duplex" &&
                                                captureDeviceCombo.currentIndex >= 0
                                                ? captureDeviceCombo.model[captureDeviceCombo.currentIndex] : null
                                        engine.configureAudioRuntime(window.runtimeDraftMode,
                                            captureDevice === null ? "" : captureDevice.id,
                                            renderDevice.id)
                                    }
                                }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Repeater {
                            model: engine.devices
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 56
                                color: colors.surface
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    Rectangle {
                                        width: 8; height: 8; radius: 4
                                        color: modelData.isDefault ? colors.healthy : colors.line
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text { text: modelData.label; color: colors.text; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                                        Text { text: modelData.id; color: colors.muted; font.pixelSize: 10; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    }
                                    Text { text: modelData.isVirtual ? "VIRTUAL" : "HARDWARE"; color: modelData.isVirtual ? colors.cyan : colors.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 20 }
                }
            }
            }

            Rectangle {
                objectName: "diagnosticsPage"
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: colors.canvas
                ScrollView {
                    id: diagnosticsScroll
                    anchors.fill: parent
                    clip: true
                ColumnLayout {
                    x: 24
                    y: 24
                    width: Math.max(0, diagnosticsScroll.availableWidth - 48)
                    spacing: 14
                    Text { text: "Diagnostics"; color: colors.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: "Live engine counters"; color: colors.muted; font.pixelSize: 12 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                    GridLayout {
                        columns: diagnosticsScroll.availableWidth > 900 ? 4 : 2
                        columnSpacing: 1
                        rowSpacing: 1
                        Layout.fillWidth: true

                        Repeater {
                            model: [
                                { label: "XRUNS", value: engine.xrunCount, tone: engine.xrunCount > 0 ? colors.warning : colors.healthy },
                                { label: "DROPPED BLOCKS", value: engine.droppedBlocks, tone: engine.droppedBlocks > 0 ? colors.danger : colors.healthy },
                                { label: "PRODUCER UNDERFLOWS", value: engine.virtualAsioProducerUnderflows, tone: engine.virtualAsioProducerUnderflows > 0 ? colors.warning : colors.healthy },
                                { label: "PRODUCER OVERFLOWS", value: engine.virtualAsioProducerOverflows, tone: engine.virtualAsioProducerOverflows > 0 ? colors.danger : colors.healthy },
                                { label: "ASIO CLIENTS", value: engine.activeClients, tone: colors.cyan },
                                { label: "CALLBACK PEAK", value: engine.callbackPeakUs.toFixed(1) + " us", tone: colors.text }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 96
                                color: colors.surface
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    Text { text: modelData.label; color: colors.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Item { Layout.fillHeight: true }
                                    Text { text: modelData.value; color: modelData.tone; font.pixelSize: 22; font.weight: Font.DemiBold }
                                }
                            }
                        }
                    }
                    Text {
                        text: "WASAPI recovery"
                        color: colors.text
                        font.pixelSize: 12
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    GridLayout {
                        columns: diagnosticsScroll.availableWidth > 900 ? 4 : 2
                        columnSpacing: 1
                        rowSpacing: 1
                        Layout.fillWidth: true

                        Repeater {
                            model: [
                                {
                                    label: "HEALTH",
                                    value: engine.wasapiRecoveryAvailable ? engine.wasapiRuntimeHealth : "Unavailable",
                                    detail: engine.wasapiRecoveryAvailable && engine.wasapiRuntimeReasonCode.length > 0
                                            ? engine.wasapiRuntimeReasonCode : "",
                                    tone: !engine.wasapiRecoveryAvailable ? colors.muted
                                          : engine.wasapiRuntimeHealth === "Healthy" ? colors.healthy
                                          : engine.wasapiRuntimeHealth === "Faulted" ? colors.danger
                                          : engine.wasapiRuntimeHealth === "Degraded" ? colors.warning
                                          : colors.muted
                                },
                                {
                                    label: "STATE",
                                    value: engine.wasapiRecoveryAvailable ? engine.wasapiRecoveryState : "Unavailable",
                                    detail: "",
                                    tone: !engine.wasapiRecoveryAvailable ? colors.muted
                                          : engine.wasapiRecoveryState === "Running" ? colors.healthy
                                          : engine.wasapiRecoveryState === "Stopped" ? colors.muted
                                          : engine.wasapiRecoveryState === "Faulted" ? colors.danger
                                          : colors.warning
                                },
                                { label: "WAIT TIMEOUTS", value: engine.wasapiWaitTimeoutCycles, detail: "cycles", tone: engine.wasapiWaitTimeoutCycles > 0 ? colors.danger : colors.healthy },
                                { label: "DISCONTINUITIES", value: engine.wasapiCaptureDiscontinuityCycles, detail: "capture cycles", tone: engine.wasapiCaptureDiscontinuityCycles > 0 ? colors.warning : colors.healthy },
                                { label: "RENDER UNDERFLOW", value: engine.wasapiRenderFifoUnderflowFrames, detail: "frames", tone: engine.wasapiRenderFifoUnderflowFrames > 0 ? colors.warning : colors.healthy },
                                { label: "RECOVERY SILENCE", value: engine.wasapiMaximumRenderRecoverySilenceFrames, detail: "max frames", tone: engine.wasapiMaximumRenderRecoverySilenceFrames > 0 ? colors.warning : colors.healthy },
                                { label: "RATE CLAMP", value: engine.wasapiMaximumConsecutiveCaptureRateClampedFrames, detail: "max frames", tone: engine.wasapiMaximumConsecutiveCaptureRateClampedFrames > 0 ? colors.warning : colors.healthy },
                                { label: "RECOVERED", value: engine.wasapiSuccessfulRecoveries + " / " + engine.wasapiRecoveryEpisodes, detail: "success / total", tone: colors.healthy },
                                { label: "FAILED", value: engine.wasapiFailedRecoveries, detail: "recoveries", tone: engine.wasapiFailedRecoveries > 0 ? colors.danger : colors.healthy },
                                { label: "LAST / MAX", value: engine.wasapiLastRecoveryMs + " / " + engine.wasapiMaximumRecoveryMs + " ms", detail: "recovery time", tone: colors.text },
                                { label: "ENDPOINT REOPENS", value: engine.wasapiEndpointReopens, detail: "notifications", tone: colors.cyan },
                                { label: "RESET FAILURES", value: engine.wasapiEndpointResetFailures, detail: "notifications", tone: engine.wasapiEndpointResetFailures > 0 ? colors.danger : colors.healthy },
                                { label: "REOPEN REQUEST", value: engine.wasapiEndpointReopenPending ? "Pending" : "Idle", detail: "", tone: engine.wasapiEndpointReopenPending ? colors.warning : colors.muted }
                            ]
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 80
                                color: colors.surface
                                ColumnLayout {
                                    anchors.fill: parent
                                    anchors.margins: 14
                                    Text { text: modelData.label; color: colors.muted; font.pixelSize: 9; font.weight: Font.DemiBold }
                                    Item { Layout.fillHeight: true }
                                    Text { text: modelData.value; color: modelData.tone; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Text { visible: modelData.detail.length > 0; text: modelData.detail; color: colors.muted; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                    Item { Layout.preferredHeight: 10 }
                }
                }
            }
        }
    }
}
