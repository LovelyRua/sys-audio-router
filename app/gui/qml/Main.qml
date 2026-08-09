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
    property string pendingRouteInputId: ""
    property string pendingRouteOutputId: ""
    property bool pendingRouteEnabled: false
    property var routeStateCache: ({})

    function routeKey(inputId, outputId) {
        return inputId.length + ":" + inputId + outputId
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
        for (var inputIndex = 0; inputIndex < engine.inputs.length; ++inputIndex) {
            if (engine.inputs[inputIndex].id === selectedInputId)
                inputLabel = engine.inputs[inputIndex].label
        }
        for (var outputIndex = 0; outputIndex < engine.outputs.length; ++outputIndex) {
            if (engine.outputs[outputIndex].id === selectedOutputId)
                outputLabel = engine.outputs[outputIndex].label
        }
        if (inputLabel.length === 0 || outputLabel.length === 0) {
            clearRouteSelection()
            return
        }
        selectedInputLabel = inputLabel
        selectedOutputLabel = outputLabel
    }

    function routeDisplayEnabled(inputId, outputId) {
        if (pendingRouteInputId === inputId && pendingRouteOutputId === outputId)
            return pendingRouteEnabled
        var route = routeStateCache[routeKey(inputId, outputId)]
        return route !== undefined && !route.muted
    }

    function routeDisplayState(inputId, outputId) {
        if (pendingRouteInputId === inputId && pendingRouteOutputId === outputId)
            return pendingRouteEnabled ? "pending-on" : "pending-off"
        var route = routeStateCache[routeKey(inputId, outputId)]
        if (route === undefined)
            return "off"
        return route.muted ? "muted" : "on"
    }

    function selectRoute(inputIndex, outputIndex) {
        if (inputIndex < 0 || inputIndex >= engine.inputs.length ||
                outputIndex < 0 || outputIndex >= engine.outputs.length)
            return false
        var input = engine.inputs[inputIndex]
        var output = engine.outputs[outputIndex]
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
        if (engine.inputs.length === 0 || engine.outputs.length === 0)
            return
        var inputIndex = endpointIndex(engine.inputs, selectedInputId)
        var outputIndex = endpointIndex(engine.outputs, selectedOutputId)
        if (inputIndex < 0 || outputIndex < 0) {
            inputIndex = 0
            outputIndex = 0
        } else {
            inputIndex = Math.max(0, Math.min(engine.inputs.length - 1,
                                             inputIndex + inputDelta))
            outputIndex = Math.max(0, Math.min(engine.outputs.length - 1,
                                              outputIndex + outputDelta))
        }
        selectRoute(inputIndex, outputIndex)
        matrixGridFlick.ensureCellVisible(inputIndex, outputIndex)
        matrixGridCanvas.requestPaint()
    }

    function toggleSelectedRoute() {
        if (!engine.connected || engine.busy || selectedInputId.length === 0)
            return
        var enabled = !routeDisplayEnabled(selectedInputId, selectedOutputId)
        pendingRouteInputId = selectedInputId
        pendingRouteOutputId = selectedOutputId
        pendingRouteEnabled = enabled
        engine.setRoute(selectedInputId, selectedOutputId, pendingRouteEnabled)
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
        function onRuntimeChanged() { window.syncRuntimeDraft() }
        function onSessionChanged() {
            window.pendingRouteInputId = ""
            window.pendingRouteOutputId = ""
            window.rebuildRouteStateCache()
            window.validateRouteSelection()
            matrixInputHeader.requestPaint()
            matrixOutputHeader.requestPaint()
            matrixGridCanvas.requestPaint()
        }
        function onBusyChanged() {
            if (!engine.busy) {
                window.pendingRouteInputId = ""
                window.pendingRouteOutputId = ""
            }
            matrixGridCanvas.requestPaint()
        }
        function onConnectionChanged() { matrixGridCanvas.requestPaint() }
    }

    onSelectedInputIdChanged: matrixGridCanvas.requestPaint()
    onSelectedOutputIdChanged: matrixGridCanvas.requestPaint()
    onPendingRouteInputIdChanged: matrixGridCanvas.requestPaint()
    onPendingRouteOutputIdChanged: matrixGridCanvas.requestPaint()
    onPendingRouteEnabledChanged: matrixGridCanvas.requestPaint()

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
                text: window.width < 1100
                      ? (engine.runtimeRunning ? "Stop" : "Start")
                      : (engine.runtimeRunning ? "Stop engine" : "Start engine")
                highlighted: !engine.runtimeRunning
                enabled: engine.connected && !engine.busy
                onClicked: engine.runtimeRunning ? engine.stopRuntime()
                                                 : engine.startRuntime()
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
                                    text: engine.inputs.length + " inputs  /  " +
                                          engine.outputs.length + " outputs"
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
                                visible: engine.inputs.length === 0 || engine.outputs.length === 0
                                text: engine.connected ? "No routable endpoints in this preset"
                                                       : "Waiting for the engine"
                                color: colors.muted
                                font.pixelSize: 13
                            }

                            Item {
                                id: matrixViewport
                                anchors.fill: parent
                                anchors.margins: 18
                                visible: engine.inputs.length > 0 && engine.outputs.length > 0
                                clip: true
                                property int labelWidth: window.width < 1100 ? 126 : 164
                                property int headerHeight: 76
                                property int cellSize: 44

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
                                        text: "OUTPUT / INPUT"
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
                                        context.fillStyle = colors.muted
                                        var cellSize = matrixViewport.cellSize
                                        var firstColumn = Math.floor(matrixGridFlick.contentX / cellSize)
                                        var x = firstColumn * cellSize - matrixGridFlick.contentX
                                        for (var column = firstColumn;
                                             column < engine.inputs.length && x < width;
                                             ++column, x += cellSize) {
                                            context.save()
                                            context.translate(x + cellSize / 2, height - 8)
                                            context.rotate(-Math.PI / 3)
                                            var label = matrixViewport.elideCanvasText(
                                                        context, engine.inputs[column].label, 96)
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
                                             row < engine.outputs.length && y < height;
                                             ++row, y += cellSize) {
                                            context.fillStyle = colors.surface
                                            context.fillRect(0, y + 1, width - 3, cellSize - 3)
                                            context.strokeStyle = colors.line
                                            context.strokeRect(0.5, y + 1.5, width - 4, cellSize - 4)
                                            context.fillStyle = colors.text
                                            context.textBaseline = "middle"
                                            var label = matrixViewport.elideCanvasText(
                                                        context, engine.outputs[row].label,
                                                        width - 18)
                                            context.fillText(label, 9, y + cellSize / 2)
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
                                                           engine.inputs.length * matrixViewport.cellSize)
                                    contentHeight: Math.max(height,
                                                            engine.outputs.length * matrixViewport.cellSize)
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
                                            anchors.fill: parent
                                            enabled: engine.connected && !engine.busy
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
                                                window.toggleSelectedRoute()
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
                                             row < engine.outputs.length && y < height;
                                             ++row, y += cellSize) {
                                            for (var column = firstColumn, x = startX;
                                                 column < engine.inputs.length && x < width;
                                                 ++column, x += cellSize) {
                                                var inputId = engine.inputs[column].id
                                                var outputId = engine.outputs[row].id
                                                var state = window.routeDisplayState(inputId, outputId)
                                                var selected = selectedInputId === inputId &&
                                                               selectedOutputId === outputId
                                                var hovered = matrixMouse.hoverInput === column &&
                                                              matrixMouse.hoverOutput === row
                                                context.fillStyle = state === "on" ? "#245a49"
                                                                  : state === "muted" ? "#4a3b20"
                                                                  : hovered && engine.connected && !engine.busy
                                                                    ? colors.hover : colors.surface
                                                context.fillRect(x + 1, y + 1,
                                                                 cellSize - 3, cellSize - 3)
                                                context.lineWidth = selected ? 2 : 1
                                                context.strokeStyle = selected ? colors.cyan
                                                                    : state === "on" ? colors.healthy
                                                                    : state === "muted" ? colors.warning
                                                                    : colors.line
                                                context.strokeRect(x + (selected ? 1 : 1.5),
                                                                   y + (selected ? 1 : 1.5),
                                                                   cellSize - (selected ? 3 : 4),
                                                                   cellSize - (selected ? 3 : 4))
                                                context.fillStyle = state === "on" ? colors.healthy
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
                            text: "Route enabled"
                            enabled: selectedInputId.length > 0 &&
                                     engine.connected && !engine.busy
                            checked: {
                                engine.routeRevision;
                                return selectedInputId.length > 0 &&
                                       window.routeDisplayEnabled(selectedInputId,
                                                                  selectedOutputId)
                            }
                            onClicked: {
                                window.pendingRouteInputId = selectedInputId
                                window.pendingRouteOutputId = selectedOutputId
                                window.pendingRouteEnabled = checked
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
                                Layout.fillWidth: true
                                from: 0
                                to: 2
                                stepSize: 0.01
                                enabled: selectedInputId.length > 0 &&
                                         routeSwitch.checked &&
                                         engine.connected && !engine.busy
                                value: {
                                    engine.routeRevision;
                                    return selectedInputId.length > 0
                                           ? engine.routeGain(selectedInputId, selectedOutputId) : 0
                                }
                                onPressedChanged: {
                                    if (!pressed) {
                                        engine.setRouteGain(selectedInputId,
                                                            selectedOutputId,
                                                            value)
                                    }
                                }
                            }
                            Text {
                                text: gainSlider.value.toFixed(2)
                                color: colors.text
                                font.family: "Consolas"
                                font.pixelSize: 11
                                Layout.preferredWidth: 36
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                        Text { text: "ASIO BUS LEVEL"; color: colors.muted; font.pixelSize: 10 }
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 12
                            radius: 2
                            color: colors.canvas
                            border.color: colors.line
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, engine.peak))
                                height: parent.height
                                radius: 2
                                color: engine.peak > 0.95 ? colors.danger
                                      : engine.peak > 0.75 ? colors.warning : colors.healthy
                            }
                        }
                        Text {
                            text: engine.peak > 0 ? (20 * Math.log(engine.peak) / Math.LN10).toFixed(1) + " dBFS"
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
                color: colors.canvas
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
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
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 1
                        clip: true
                        model: engine.devices
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: 56
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
            }

            Rectangle {
                color: colors.canvas
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14
                    Text { text: "Diagnostics"; color: colors.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: "Live engine counters"; color: colors.muted; font.pixelSize: 12 }
                    Rectangle { Layout.fillWidth: true; height: 1; color: colors.line }
                    GridLayout {
                        columns: width > 900 ? 4 : 2
                        columnSpacing: 1
                        rowSpacing: 1
                        Layout.fillWidth: true

                        Repeater {
                            model: [
                                { label: "XRUNS", value: engine.xrunCount, tone: engine.xrunCount > 0 ? colors.warning : colors.healthy },
                                { label: "DROPPED BLOCKS", value: engine.droppedBlocks, tone: engine.droppedBlocks > 0 ? colors.danger : colors.healthy },
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
                        columns: width > 900 ? 4 : 2
                        columnSpacing: 1
                        rowSpacing: 1
                        Layout.fillWidth: true

                        Repeater {
                            model: [
                                {
                                    label: "STATE",
                                    value: engine.wasapiRecoveryAvailable ? engine.wasapiRecoveryState : "Unavailable",
                                    tone: !engine.wasapiRecoveryAvailable ? colors.muted
                                          : engine.wasapiRecoveryState === "Running" ? colors.healthy
                                          : engine.wasapiRecoveryState === "Stopped" ? colors.muted
                                          : engine.wasapiRecoveryState === "Faulted" ? colors.danger
                                          : colors.warning
                                },
                                { label: "RECOVERED", value: engine.wasapiSuccessfulRecoveries + " / " + engine.wasapiRecoveryEpisodes, tone: colors.healthy },
                                { label: "FAILED", value: engine.wasapiFailedRecoveries, tone: engine.wasapiFailedRecoveries > 0 ? colors.danger : colors.healthy },
                                { label: "LAST / MAX", value: engine.wasapiLastRecoveryMs + " / " + engine.wasapiMaximumRecoveryMs + " ms", tone: colors.text },
                                { label: "ENDPOINT REOPENS", value: engine.wasapiEndpointReopens, tone: colors.cyan },
                                { label: "RESET FAILURES", value: engine.wasapiEndpointResetFailures, tone: engine.wasapiEndpointResetFailures > 0 ? colors.danger : colors.healthy },
                                { label: "REOPEN REQUEST", value: engine.wasapiEndpointReopenPending ? "Pending" : "Idle", tone: engine.wasapiEndpointReopenPending ? colors.warning : colors.muted }
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
                                }
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
