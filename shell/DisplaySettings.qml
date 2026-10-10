// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The display settings window, as Windows' Display settings and KDE's Display Configuration: the
// monitors drawn to scale where they stand, to click and to drag (a dragged one snaps beside the
// others, edges in line), those off or mirroring under them, the selected monitor's settings, and
// Apply, which puts them all on trial. "Keep these display settings?" then counts down, and the
// compositor takes them back unless they are kept; Revert, Escape and closing take them back at
// once. The keyboard starts on Revert, as on Windows: a monitor showing nothing keeps nothing on
// a stray Enter. Reset to configuration puts the configuration's settings on trial, and once they
// are kept the window's are gone. In the macOS style it is titled Displays, as macOS's pane is.
Item {
    id: root
    objectName: "displaySettings"
    required property size screenSize
    readonly property var settings: shell.displaySettings
    readonly property var current: settings.current
    focus: true
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }
    readonly property int margin: Math.max(Theme.spacingL, Theme.shadowMargin)
    width: card.width + 2 * margin
    height: card.height + 2 * margin

    // Opened, or opened again: the keyboard on the window, or on Revert while a trial runs.
    function reset() {
        if (settings.trial)
            revertButton.forceActiveFocus()
        else
            root.forceActiveFocus()
    }
    Connections {
        target: root.settings
        function onTrialChanged() { root.reset() }
    }
    Keys.onEscapePressed: settings.trial ? settings.revert() : settings.close()
    Keys.onReturnPressed: (event) => root.press(event)
    Keys.onEnterPressed: (event) => root.press(event)
    function press(event) {
        if (keepButton.activeFocus)
            settings.keep()
        else if (revertButton.activeFocus)
            settings.revert()
        else
            event.accepted = false
    }

    // A monitor's entries of `settings.monitors` by name.
    function monitor(name) {
        var monitors = settings.monitors
        for (var i = 0; i < monitors.length; ++i)
            if (monitors[i].name === name)
                return monitors[i]
        return null
    }
    // What the monitors out of the arrangement do: off, or mirroring another.
    function outOfArrangement(entry) {
        return !entry.enabled ? "Off" : "Mirrors " + entry.mirror
    }
    // The choices of a setting of the selected monitor, as {label, value}.
    function scaleChoices() {
        var scale = current.scale || 1
        var list = settings.scales().map(function(value) { return { label: Math.round(value * 100) + "%", value: value } })
        if (!list.some(function(entry) { return Math.abs(entry.value - scale) < 0.001 }))
            list.push({ label: Math.round(scale * 100) + "%", value: scale })
        list.sort(function(a, b) { return a.value - b.value })
        list.push({ label: "Custom…", value: 0 })
        return list
    }
    function mirrorChoices() {
        var list = [{ label: "Its own desktop", value: "" }]
        var monitors = settings.monitors
        for (var i = 0; i < monitors.length; ++i)
            if (monitors[i].name !== current.name && (monitors[i].inLayout || monitors[i].name === current.mirror))
                list.push({ label: "Same as " + monitors[i].name, value: monitors[i].name })
        return list
    }
    function transformChoices() {
        var list = []
        for (var i = 0; i < 8; ++i)
            list.push({ label: settings.transformName(i), value: i })
        return list
    }
    function indexOf(list, test) {
        for (var i = 0; i < list.length; ++i)
            if (test(list[i]))
                return i
        return -1
    }
    // What the selected monitor's settings cannot show by themselves, a line each.
    readonly property string note: {
        if (!current.name)
            return ""
        var lines = []
        if (current.state === "lid")
            lines.push("Off while the laptop's lid is closed.")
        if (current.mirror)
            lines.push("Shows " + current.mirror + "'s picture, fitted to its own resolution; its scale does not matter.")
        if (!current.hdrPossible)
            lines.push("HDR cannot be had: " + current.hdrWhy + ".")
        else if (current.hdr && !current.hdrActive && !settings.changed)
            lines.push("HDR is asked for, but the monitor stayed SDR; the log says why.")
        if (current.bitDepth === 10 && current.drawnDepth === 8 && !settings.changed)
            lines.push("10 bits are asked for, but it is drawn in 8: the monitor, the cable or the graphics card refused them.")
        return lines.join("\n")
    }

    // A choice from a list, as {label, value}: `selectedIndex` is the one in force, and `picked`
    // says which the user took; what is in force then stays bound.
    component Choice: ComboBox {
        id: choice
        property int selectedIndex: -1
        signal picked(int index)
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: Theme.buttonHeight
        textRole: "label"
        currentIndex: selectedIndex
        onActivated: (index) => {
            choice.picked(index)
            choice.currentIndex = Qt.binding(function() { return choice.selectedIndex })
        }
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
        background: Rectangle {
            radius: Theme.radiusSmall
            color: choice.hovered ? Theme.buttonFaceHover : Theme.buttonFace
            border.color: choice.visualFocus ? Theme.focusRing : Theme.buttonOutline
            border.width: choice.visualFocus ? Theme.focusRingWidth : 1
        }
        contentItem: Text {
            leftPadding: Theme.spacingM
            rightPadding: Theme.iconSizeSmall + Theme.spacingM
            text: choice.displayText
            color: choice.enabled ? Theme.text : Theme.textDisabled
            font: choice.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        indicator: Icon {
            x: choice.width - width - Theme.spacingM
            y: (choice.height - height) / 2
            name: "chevron-right"; rotation: 90
            size: Theme.iconSizeSmall
            color: choice.enabled ? Theme.textMuted : Theme.textDisabled
        }
        delegate: ItemDelegate {
            id: row
            required property var modelData
            required property int index
            width: ListView.view ? ListView.view.width : implicitWidth
            implicitHeight: Theme.menuRowHeight
            highlighted: choice.highlightedIndex === index
            contentItem: Text {
                text: row.modelData.label
                color: row.highlighted ? Theme.menuHighlightText : Theme.text
                font: choice.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            background: Rectangle {
                radius: Theme.menuRowRadius
                color: row.highlighted ? Theme.menuHighlight : "transparent"
            }
        }
        popup: Popup {
            y: choice.height + Theme.spacingXS
            width: choice.width
            margins: Theme.spacingM
            padding: Theme.menuPadding
            implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding, 8 * Theme.menuRowHeight + 2 * padding)
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: choice.popup.visible ? choice.delegateModel : null
                currentIndex: choice.highlightedIndex
                boundsBehavior: Flickable.StopAtBounds
                ScrollIndicator.vertical: ScrollIndicator {}
            }
            background: Rectangle {
                radius: Theme.menuRadius
                color: Theme.popupSurface
                border.color: Theme.popupOutline
            }
        }
    }
    // A switch, as Notification Center's do-not-disturb: `on` is what is in force, and `turned`
    // says what the user made it; what is in force then stays bound, so that a change the model
    // does not take shows as not taken.
    component Toggle: Switch {
        id: toggle
        property bool on: false
        signal turned(bool on)
        checked: on
        onToggled: {
            toggle.turned(checked)
            checked = Qt.binding(function() { return toggle.on })
        }
        implicitHeight: Theme.buttonHeight
        indicator: Rectangle {
            x: toggle.leftPadding; y: parent.height / 2 - height / 2
            width: 2 * height; height: Theme.iconSize; radius: height / 2
            opacity: toggle.enabled ? 1 : 0.5
            color: toggle.checked ? Theme.accent : Theme.macos ? Theme.switchTrack : Theme.selected
            border.color: toggle.visualFocus ? Theme.focusRing : "transparent"
            Rectangle {
                x: toggle.checked ? parent.width - width - Theme.spacingXS : Theme.spacingXS
                y: Theme.spacingXS; width: parent.height - 2 * Theme.spacingXS; height: width; radius: width / 2
                color: Theme.macos ? Theme.knob : toggle.checked ? Theme.textOnAccent : Theme.text
                border.color: Theme.macos ? Theme.knobOutline : "transparent"
                Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
        }
        contentItem: Item {}
    }
    // A setting's name, before its control.
    component SettingLabel: Text {
        Layout.preferredWidth: 110
        color: enabled ? Theme.textMuted : Theme.textDisabled
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
        elide: Text.ElideRight
    }
    // What stands in a control's place where the monitor has no such setting.
    component Unavailable: Text {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Layout.preferredHeight: Theme.buttonHeight
        verticalAlignment: Text.AlignVCenter
        color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
        elide: Text.ElideRight
    }

    Item {
        id: card
        objectName: "displaySettingsCard"
        anchors.centerIn: parent
        width: Math.min(760, root.screenSize.width - 2 * root.margin)
        height: Math.min(column.implicitHeight + 2 * Theme.spacingXL, root.screenSize.height - 2 * root.margin)
        opacity: root.progress
        scale: 0.96 + 0.04 * root.progress
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.radiusLarge
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.popupSurface
            border.color: Theme.popupOutline
        }
        MouseArea { anchors.fill: parent; onPressed: root.forceActiveFocus() }
        ColumnLayout {
            id: column
            anchors.fill: parent
            anchors.margins: Theme.spacingXL
            spacing: Theme.spacingL
            // The title, and the cross that closes the window, which takes a trial back too.
            RowLayout {
                id: header
                Layout.fillWidth: true
                spacing: Theme.spacingM
                Icon { name: "monitor"; size: Theme.iconSize; color: Theme.text }
                Text {
                    Layout.fillWidth: true
                    text: Theme.macos ? "Displays" : "Display settings"
                    color: Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                CloseButton {
                    objectName: "displaySettingsClose"
                    Accessible.name: "Close"
                    onClicked: root.settings.close()
                }
            }
            // The arrangement: each monitor showing a desktop of its own, to scale, where it
            // stands. A dragged one shows where it would snap.
            Rectangle {
                id: arrangement
                objectName: "displayArrangement"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(120, Math.min(200, root.screenSize.height / 5))
                radius: Theme.radiusMedium
                color: Theme.surfaceRaised
                border.color: Theme.border
                readonly property real pad: Theme.spacingXL
                readonly property size extent: root.settings.extent
                // Logical pixels to the drawing's, and where the arrangement starts in it.
                readonly property real ratio: Math.min((width - 2 * pad) / Math.max(1, extent.width),
                                                       (height - 2 * pad) / Math.max(1, extent.height))
                readonly property real originX: (width - extent.width * ratio) / 2
                readonly property real originY: (height - extent.height * ratio) / 2
                // Edges come in line within this many logical pixels: eight of the drawing's.
                readonly property int threshold: Math.round(8 / Math.max(0.001, ratio))
                function logicalX(x) { return Math.round((x - originX) / ratio) }
                function logicalY(y) { return Math.round((y - originY) / ratio) }
                // Where the tile being dragged would go.
                function follow(tile) {
                    var at = root.settings.snapped(tile.modelData.name, logicalX(tile.x), logicalY(tile.y), threshold)
                    ghost.x = originX + at.x * ratio
                    ghost.y = originY + at.y * ratio
                    ghost.width = tile.width
                    ghost.height = tile.height
                    ghost.visible = true
                }
                Rectangle {
                    id: ghost
                    objectName: "displayGhost"
                    visible: false
                    radius: Theme.radiusSmall
                    color: "transparent"
                    border.color: Theme.accent
                    border.width: 2
                }
                Repeater {
                    model: root.settings.monitors
                    delegate: Rectangle {
                        id: tile
                        required property var modelData
                        readonly property bool chosen: modelData.name === root.settings.selected
                        property bool dragged: false
                        objectName: "displayTile:" + modelData.name
                        visible: modelData.inLayout
                        x: arrangement.originX + modelData.x * arrangement.ratio
                        y: arrangement.originY + modelData.y * arrangement.ratio
                        width: Math.max(1, modelData.logicalWidth * arrangement.ratio)
                        height: Math.max(1, modelData.logicalHeight * arrangement.ratio)
                        z: dragged ? 2 : chosen ? 1 : 0
                        radius: Theme.radiusSmall
                        color: chosen ? Theme.mix(Theme.surface, Theme.accent, 0.18) : Theme.surface
                        border.color: chosen ? Theme.accent : Theme.border
                        border.width: chosen ? 2 : 1
                        opacity: modelData.state === "lid" ? 0.55 : 1
                        Column {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingS
                            spacing: Theme.spacingXS
                            clip: true
                            Text {
                                width: parent.width
                                text: tile.modelData.name + (tile.modelData.primary ? " ★" : "")
                                color: Theme.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: tile.modelData.title
                                color: Theme.textMuted
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                visible: tile.modelData.mirroredBy.length > 0 || tile.modelData.state === "lid"
                                text: tile.modelData.state === "lid" ? "Lid closed"
                                      : "Mirrored by " + tile.modelData.mirroredBy.join(", ")
                                color: Theme.textMuted
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                                elide: Text.ElideRight
                            }
                        }
                        MouseArea {
                            anchors.fill: parent
                            enabled: !root.settings.trial
                            cursorShape: Qt.PointingHandCursor
                            drag.target: root.settings.layoutCount > 1 ? tile : null
                            drag.threshold: 4
                            onPressed: {
                                root.forceActiveFocus()
                                root.settings.select(tile.modelData.name)
                            }
                            onPositionChanged: {
                                if (!drag.active)
                                    return
                                tile.dragged = true
                                arrangement.follow(tile)
                            }
                            onReleased: {
                                ghost.visible = false
                                if (!tile.dragged)
                                    return
                                tile.dragged = false
                                // The model places it, and the tiles are drawn anew where it says.
                                root.settings.place(tile.modelData.name, arrangement.logicalX(tile.x),
                                                    arrangement.logicalY(tile.y), arrangement.threshold)
                            }
                        }
                    }
                }
            }
            // The monitors out of the arrangement: off, or showing another's picture.
            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                visible: root.settings.monitors.some(function(entry) { return !entry.inLayout })
                Repeater {
                    model: root.settings.monitors.filter(function(entry) { return !entry.inLayout })
                    delegate: PushButton {
                        required property var modelData
                        objectName: "displayChip:" + modelData.name
                        small: true
                        iconName: modelData.builtIn ? "laptop" : "monitor"
                        text: modelData.name + " · " + root.outOfArrangement(modelData)
                        current: modelData.name === root.settings.selected
                        focusPolicy: Qt.NoFocus
                        enabled: !root.settings.trial
                        onClicked: root.settings.select(modelData.name)
                    }
                }
            }
            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }
            // The selected monitor, and its settings in two columns.
            Text {
                objectName: "displaySelected"
                Layout.fillWidth: true
                text: !root.current.name ? "No monitor is connected"
                      : root.current.title + (root.current.title !== root.current.name ? " — " + root.current.name : "")
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.weight: Font.Medium
                elide: Text.ElideRight
            }
            GridLayout {
                id: form
                Layout.fillWidth: true
                columns: 4
                columnSpacing: Theme.spacingL
                rowSpacing: Theme.spacingM
                enabled: !!root.current.name && !root.settings.trial
                // Whether the selected monitor's own settings are in use: on, or mirroring.
                readonly property bool on: !!root.current.enabled

                SettingLabel { text: "Display" }
                Item {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.buttonHeight
                    Toggle {
                        objectName: "displayEnabled"
                        anchors.verticalCenter: parent.verticalCenter
                        on: !!root.current.enabled
                        // The last monitor showing the desktop stays on.
                        enabled: !(root.current.inLayout && root.settings.layoutCount === 1)
                        onTurned: (on) => root.settings.setEnabled(root.current.name, on)
                        Accessible.name: "Use this display"
                    }
                }
                SettingLabel { text: "Picture"; enabled: form.on }
                Choice {
                    id: mirrorChoice
                    objectName: "displayMirror"
                    enabled: form.on && root.settings.monitors.length > 1
                    model: root.mirrorChoices()
                    selectedIndex: root.indexOf(model, function(entry) { return entry.value === (root.current.mirror || "") })
                    onPicked: (index) => root.settings.setMirror(root.current.name, model[index].value)
                }

                SettingLabel { text: "Resolution"; enabled: form.on }
                Choice {
                    objectName: "displayResolution"
                    enabled: form.on
                    model: root.current.resolutions || []
                    selectedIndex: root.indexOf(model, function(entry) {
                        return entry.width === root.current.width && entry.height === root.current.height
                    })
                    onPicked: (index) => root.settings.setResolution(root.current.name, model[index].width, model[index].height)
                }
                SettingLabel { text: "Refresh rate"; enabled: form.on }
                Choice {
                    objectName: "displayRefresh"
                    enabled: form.on
                    model: root.current.rates || []
                    selectedIndex: root.indexOf(model, function(entry) { return entry.refresh === root.current.refresh })
                    onPicked: (index) => root.settings.setRefresh(root.current.name, model[index].refresh)
                }

                SettingLabel { text: "Scale"; enabled: form.on && !root.current.mirror }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    spacing: Theme.spacingS
                    Choice {
                        id: scaleChoice
                        objectName: "displayScale"
                        visible: !customScale.visible
                        enabled: form.on && !root.current.mirror
                        model: root.scaleChoices()
                        selectedIndex: root.indexOf(model, function(entry) { return Math.abs(entry.value - (root.current.scale || 1)) < 0.001 })
                        onPicked: (index) => {
                            if (model[index].value > 0) {
                                root.settings.setScale(root.current.name, model[index].value)
                                return
                            }
                            customScale.text = Math.round((root.current.scale || 1) * 100)
                            customScale.visible = true
                            customScale.forceActiveFocus()
                            customScale.selectAll()
                        }
                    }
                    // A scale of one's own, in per cent, taken with Enter or as the field is left.
                    TextField {
                        id: customScale
                        objectName: "displayScaleCustom"
                        visible: false
                        Layout.fillWidth: true
                        implicitHeight: Theme.buttonHeight
                        validator: IntValidator { bottom: 25; top: 1000 }
                        color: Theme.text
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.textOnAccent
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                        verticalAlignment: TextInput.AlignVCenter
                        leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
                        background: Rectangle {
                            radius: Theme.radiusSmall
                            color: Theme.fieldFill
                            border.color: customScale.activeFocus ? Theme.accent : Theme.border
                        }
                        function take() {
                            if (!visible)
                                return
                            visible = false
                            if (acceptableInput)
                                root.settings.setScale(root.current.name, parseInt(text) / 100)
                            root.forceActiveFocus()
                        }
                        onAccepted: take()
                        onActiveFocusChanged: if (!activeFocus) take()
                        Keys.onEscapePressed: { visible = false; root.forceActiveFocus() }
                    }
                    Text {
                        visible: customScale.visible
                        text: "%"
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                    }
                }
                SettingLabel { text: "Rotation"; enabled: form.on }
                Choice {
                    objectName: "displayRotation"
                    enabled: form.on
                    model: root.transformChoices()
                    selectedIndex: root.current.transform || 0
                    onPicked: (index) => root.settings.setTransform(root.current.name, model[index].value)
                }

                SettingLabel { text: "Adaptive sync"; enabled: form.on }
                Item {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.buttonHeight
                    Toggle {
                        objectName: "displayVrr"
                        visible: !!root.current.vrrSupported
                        enabled: form.on
                        anchors.verticalCenter: parent.verticalCenter
                        on: !!root.current.vrr
                        onTurned: (on) => root.settings.setVrr(root.current.name, on)
                        Accessible.name: "Adaptive sync"
                    }
                    Unavailable {
                        anchors.fill: parent
                        visible: !root.current.vrrSupported
                        text: "Not offered"
                    }
                }
                SettingLabel { text: "10-bit colour"; enabled: form.on }
                Item {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.buttonHeight
                    Toggle {
                        objectName: "displayDeep"
                        enabled: form.on && !root.current.hdr
                        anchors.verticalCenter: parent.verticalCenter
                        // HDR draws in 10 bits where it can.
                        on: root.current.bitDepth === 10 || !!root.current.hdr
                        onTurned: (on) => root.settings.setBitDepth(root.current.name, on ? 10 : 8)
                        Accessible.name: "10 bits per colour channel"
                    }
                }

                SettingLabel { text: "HDR"; enabled: form.on }
                Item {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.buttonHeight
                    // Only where the compositor says it can be had; else why not, below.
                    Toggle {
                        objectName: "displayHdr"
                        visible: !!root.current.hdrPossible
                        enabled: form.on && !root.current.mirror
                        anchors.verticalCenter: parent.verticalCenter
                        on: !!root.current.hdr
                        onTurned: (on) => root.settings.setHdr(root.current.name, on)
                        Accessible.name: "HDR"
                    }
                    Unavailable {
                        objectName: "displayHdrUnavailable"
                        anchors.fill: parent
                        visible: !root.current.hdrPossible
                        text: "Not available"
                    }
                }
                SettingLabel { text: "Main display"; enabled: form.on }
                Item {
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    Layout.preferredHeight: Theme.buttonHeight
                    PushButton {
                        objectName: "displayMakePrimary"
                        visible: !root.current.primary
                        enabled: !!root.current.inLayout
                        anchors.verticalCenter: parent.verticalCenter
                        small: true
                        text: "Make this the main display"
                        onClicked: root.settings.setPrimary(root.current.name)
                    }
                    Unavailable {
                        anchors.fill: parent
                        visible: !!root.current.primary
                        text: "This is the main display"
                    }
                }
            }
            Text {
                objectName: "displayNote"
                Layout.fillWidth: true
                visible: text !== ""
                text: root.note
                wrapMode: Text.Wrap
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
            }
            Item { Layout.fillHeight: true }
            // What happened, and the buttons.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingM
                PushButton {
                    objectName: "displayReset"
                    small: true
                    text: "Reset to configuration"
                    enabled: root.settings.kept && !root.settings.busy && !root.settings.trial
                    onClicked: root.settings.reset()
                }
                Text {
                    objectName: "displayMessage"
                    Layout.fillWidth: true
                    text: root.settings.message
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
                PushButton {
                    objectName: "displayUndo"
                    text: "Undo changes"
                    enabled: root.settings.changed && !root.settings.busy && !root.settings.trial
                    onClicked: root.settings.reload()
                }
                PushButton {
                    objectName: "displayApply"
                    text: "Apply"
                    primary: true
                    enabled: root.settings.changed && !root.settings.busy && !root.settings.trial
                    onClicked: root.settings.apply()
                }
            }
        }
        // The question while the settings are on trial, over the window but for its title.
        Rectangle {
            id: question
            objectName: "displayTrial"
            anchors.fill: parent
            anchors.topMargin: column.y + header.y + header.height + Theme.spacingS
            radius: Theme.radiusLarge
            topLeftRadius: 0; topRightRadius: 0
            color: Theme.alpha(Theme.popupSurface, 0.82)
            opacity: root.settings.trial ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            MouseArea { anchors.fill: parent }
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(420, parent.width - 2 * Theme.spacingXL)
                height: ask.implicitHeight + 2 * Theme.spacingXL
                radius: Theme.radiusLarge
                color: Theme.popupSurface
                border.color: Theme.popupOutline
                ColumnLayout {
                    id: ask
                    anchors.fill: parent
                    anchors.margins: Theme.spacingXL
                    spacing: Theme.spacingL
                    Text {
                        Layout.fillWidth: true
                        text: "Keep these display settings?"
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                        wrapMode: Text.Wrap
                    }
                    Text {
                        objectName: "displayCountdown"
                        Layout.fillWidth: true
                        text: "The previous settings come back in " + root.settings.secondsLeft
                              + (root.settings.secondsLeft === 1 ? " second." : " seconds.")
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                        wrapMode: Text.Wrap
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignRight
                        spacing: Theme.spacingM
                        PushButton {
                            id: revertButton
                            objectName: "displayRevert"
                            Layout.minimumWidth: 3 * Theme.rowHeight
                            text: "Revert"
                            onClicked: root.settings.revert()
                        }
                        PushButton {
                            id: keepButton
                            objectName: "displayKeep"
                            Layout.minimumWidth: 3 * Theme.rowHeight
                            text: "Keep changes"
                            primary: true
                            onClicked: root.settings.keep()
                        }
                    }
                }
            }
        }
    }
}
