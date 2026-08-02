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
        return deviceId.length === 0 && model.length > 0 && !runtimeDraftDirty ? 0 : -1
    }

    Connections {
        target: engine
        function onRuntimeChanged() { window.syncRuntimeDraft() }
    }

    Component.onCompleted: syncRuntimeDraft()

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
        implicitHeight: 34
        leftPadding: 10
        rightPadding: 28
        font.pixelSize: 12
        contentItem: Text {
            text: control.displayText.length > 0 ? control.displayText
                                                 : "No saved presets"
            color: control.displayText.length > 0 ? colors.text : colors.muted
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
                text: control.textRole.length > 0 && modelData !== null
                      && typeof modelData === "object"
                      ? modelData[control.textRole] : modelData
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
                                Item { Layout.fillWidth: true }
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

                            Flickable {
                                id: matrixFlick
                                anchors.fill: parent
                                anchors.margins: 18
                                visible: engine.inputs.length > 0 && engine.outputs.length > 0
                                contentWidth: matrixColumn.width
                                contentHeight: matrixColumn.height
                                clip: true
                                boundsBehavior: Flickable.StopAtBounds
                                ScrollBar.horizontal: ScrollBar {}
                                ScrollBar.vertical: ScrollBar {}

                                Column {
                                    id: matrixColumn
                                    spacing: 3
                                    property int labelWidth: 154
                                    property int cellSize: 44

                                    Row {
                                        spacing: 3
                                        Item { width: matrixColumn.labelWidth; height: 72 }
                                        Repeater {
                                            model: engine.inputs
                                            delegate: Item {
                                                required property var modelData
                                                width: matrixColumn.cellSize
                                                height: 72
                                                Text {
                                                    anchors.centerIn: parent
                                                    width: 68
                                                    text: modelData.label
                                                    color: colors.muted
                                                    font.pixelSize: 10
                                                    horizontalAlignment: Text.AlignHCenter
                                                    elide: Text.ElideRight
                                                    rotation: -55
                                                }
                                            }
                                        }
                                    }

                                    Repeater {
                                        model: engine.outputs
                                        delegate: Row {
                                            id: outputRow
                                            required property var modelData
                                            property int outputIndex: index
                                            spacing: 3
                                            height: matrixColumn.cellSize

                                            Rectangle {
                                                width: matrixColumn.labelWidth
                                                height: matrixColumn.cellSize
                                                color: colors.surface
                                                border.color: colors.line
                                                radius: 3
                                                Text {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 8
                                                    text: outputRow.modelData.label
                                                    color: colors.text
                                                    font.pixelSize: 11
                                                    verticalAlignment: Text.AlignVCenter
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Repeater {
                                                model: engine.inputs
                                                delegate: Button {
                                                    id: routeCell
                                                    required property var modelData
                                                    property int revision: engine.routeRevision
                                                    property string inputId: modelData.id
                                                    property string outputId: outputRow.modelData.id
                                                    property bool active: {
                                                        revision;
                                                        return engine.routeEnabled(inputId, outputId)
                                                    }
                                                    width: matrixColumn.cellSize
                                                    height: matrixColumn.cellSize
                                                    padding: 0
                                                    enabled: engine.connected && !engine.busy
                                                    background: Rectangle {
                                                        radius: 3
                                                        color: routeCell.active ? "#245a49"
                                                                                : routeCell.hovered ? colors.hover : colors.surface
                                                        border.width: selectedInputId === routeCell.inputId &&
                                                                      selectedOutputId === routeCell.outputId ? 2 : 1
                                                        border.color: selectedInputId === routeCell.inputId &&
                                                                      selectedOutputId === routeCell.outputId
                                                                      ? colors.cyan
                                                                      : routeCell.active ? colors.healthy : colors.line
                                                        Rectangle {
                                                            width: 8
                                                            height: 8
                                                            radius: 4
                                                            anchors.centerIn: parent
                                                            color: routeCell.active ? colors.healthy : colors.line
                                                        }
                                                    }
                                                    onClicked: {
                                                        selectedInputId = inputId
                                                        selectedInputLabel = modelData.label
                                                        selectedOutputId = outputId
                                                        selectedOutputLabel = outputRow.modelData.label
                                                        engine.setRoute(inputId, outputId, !active)
                                                    }
                                                }
                                            }
                                        }
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
                                       engine.routeEnabled(selectedInputId, selectedOutputId)
                            }
                            onClicked: engine.setRoute(selectedInputId, selectedOutputId, checked)
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
                                    model: engine.devices.filter(function (device) {
                                        return device.direction === 1 || device.direction === 2
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
                                    model: engine.devices.filter(function (device) {
                                        return device.direction === 0 || device.direction === 2
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
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
