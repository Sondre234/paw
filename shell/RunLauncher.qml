// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

// The run launcher (shell.runLauncher; the run action, Super + R), after rofi: the modes as pills
// along the top, the one in use under a pill with a cat's ears that slides between them; a line
// to type in after a cat that watches what happens; and what that finds under it, the letters it
// matched in the accent, each row easing in as it comes and gliding as the list changes. Tab and
// Shift + Tab change the mode, Up and Down (Ctrl + N and P, Ctrl + J and K) choose, Enter runs
// what is chosen (with a little hop), Ctrl + Enter the text as typed, Shift + Enter in a terminal,
// and Escape closes. A card as the command palette's, coming in each time its window shows, of a
// fixed height so that typing never resizes it.
Item {
    id: root
    required property size screenSize
    readonly property var launcher: shell.runLauncher
    readonly property int padding: Theme.spacingL
    readonly property int rowHeight: Theme.rowHeight + Theme.spacingXS
    readonly property int visibleRows: Math.max(3, Math.min(8, Math.floor((screenSize.height * 0.6 - 140) / rowHeight)))
    readonly property string query: launcher.query.trim()
    // Enter was pressed: the cat cheers and the chosen row hops as the card goes.
    property bool launching: false
    // The cat's face, by what is going on.
    readonly property string face: launching ? "\\(=^▽^=)/"
                                 : launcher.problem !== "" ? "(=TωT=)"
                                 : query !== "" && list.count === 0 ? "(=;ω;=)"
                                 : query !== "" ? "(=ↀωↀ=)"
                                 : "(=^·ω·^=)"
    readonly property var placeholders: ["Launch an application…", "Run a command…", "Go to a window…"]
    readonly property var modeIcons: ["layout-grid", "terminal", "app-window"]
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start), as
    // the palette's: `progress` follows, from 0 to 1. After a launch it goes slower, for the hop.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal {
        id: reveal
        shown: root.shown
        exitDuration: root.launching ? Theme.durationSlow : Theme.durationFast
    }
    // The rows shown ease in, one after another, each time it opens.
    signal opened()

    function reset() {
        launching = false
        pointerSeen = false
        input.text = launcher.query
        list.positionViewAtBeginning()
        input.forceActiveFocus()
        opened()
    }
    Connections {
        target: root.launcher
        function onQueryChanged() { if (input.text !== root.launcher.query) input.text = root.launcher.query }
        // A new search selects the first row again, which comes into view.
        function onSelectedChanged() { list.positionViewAtIndex(root.launcher.selected, ListView.Contain) }
    }
    // Where the pointer last was over the card, so that a row the list moves under a pointer at
    // rest is not selected: only the pointer moving selects, not where it is as the card opens.
    property point pointer: Qt.point(-1, -1)
    property bool pointerSeen: false
    // Runs what Enter asks for, the chosen row hopping first when it is what runs.
    function run(how) {
        var row = list.currentItem
        var ran = how === "typed" ? launcher.runTyped()
                : how === "terminal" ? launcher.runInTerminal()
                : launcher.activate()
        if (!ran)
            return
        launching = true
        if (row && how !== "typed")
            row.hop()
    }
    function key(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var shift = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_Down || (ctrl && (event.key === Qt.Key_N || event.key === Qt.Key_J))) {
            launcher.move(1)
        } else if (event.key === Qt.Key_Up || (ctrl && (event.key === Qt.Key_P || event.key === Qt.Key_K))) {
            launcher.move(-1)
        } else if (event.key === Qt.Key_PageDown) {
            launcher.move(Math.min(root.visibleRows, list.count - 1 - launcher.selected))
        } else if (event.key === Qt.Key_PageUp) {
            launcher.move(-Math.min(root.visibleRows, launcher.selected))
        } else if (event.key === Qt.Key_Tab) {
            launcher.cycle(1)
        } else if (event.key === Qt.Key_Backtab) {
            launcher.cycle(-1)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            run(ctrl ? "typed" : shift ? "terminal" : "chosen")
        } else if (event.key === Qt.Key_Escape) {
            launcher.close()
        } else {
            return
        }
        event.accepted = true
    }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        width: Math.min(620, root.screenSize.width - 32)
        height: footer.y + footer.height + Theme.spacingM
        opacity: root.progress
        scale: 0.97 + 0.03 * root.progress
        transform: Translate { y: (root.progress - 1) * Theme.spacingM }
        CardFrame { radius: Theme.spotlightRadius; innerEdge: true }

        // The modes, the one in use under a pill in the accent fading into the blush, with a cat's
        // ears, which slides to the next; and how far down the list the selection is.
        Item {
            id: modes
            x: root.padding; y: root.padding
            width: card.width - 2 * root.padding
            height: 28
            Item {
                id: pill
                readonly property Item at: modeButtons.count > 0 ? modeButtons.itemAt(root.launcher.mode) : null
                x: at ? at.x : 0
                width: at ? at.width : 0
                height: parent.height
                // It slides once the card is in: opening, it is in the first mode at once.
                Behavior on x {
                    enabled: root.progress > 0.99
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack; easing.overshoot: 1.2 }
                }
                Behavior on width {
                    enabled: root.progress > 0.99
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing }
                }
                readonly property color fromColor: Theme.mix(Theme.popupSurface, Theme.accent, Theme.light ? 0.32 : 0.5)
                readonly property color toColor: Theme.mix(Theme.popupSurface, Theme.blush, Theme.light ? 0.36 : 0.52)
                // The ears, standing up from the pill's top edge a little in from its ends.
                Shape {
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeWidth: -1
                        fillColor: pill.fromColor
                        startX: 8; startY: 3
                        PathLine { x: 12; y: -7 }
                        PathLine { x: 21; y: 1 }
                    }
                    ShapePath {
                        strokeWidth: -1
                        fillColor: pill.toColor
                        startX: pill.width - 21; startY: 1
                        PathLine { x: pill.width - 12; y: -7 }
                        PathLine { x: pill.width - 8; y: 3 }
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: pill.fromColor }
                        GradientStop { position: 1; color: pill.toColor }
                    }
                }
            }
            Row {
                id: modeRow
                height: parent.height
                spacing: Theme.spacingXS
                Repeater {
                    id: modeButtons
                    model: root.launcher.modes
                    delegate: Item {
                        id: modeButton
                        required property string modelData
                        required property int index
                        readonly property bool active: root.launcher.mode === index
                        objectName: "runMode:" + modelData
                        width: modeLabel.implicitWidth + modeIcon.width + Theme.spacingS + 2 * Theme.spacingL
                        height: modeRow.height
                        Rectangle {
                            anchors.fill: parent
                            radius: height / 2
                            color: modeArea.containsMouse && !modeButton.active ? Theme.hover : "transparent"
                        }
                        Row {
                            anchors.centerIn: parent
                            spacing: Theme.spacingS
                            Icon {
                                id: modeIcon
                                anchors.verticalCenter: parent.verticalCenter
                                name: root.modeIcons[modeButton.index]
                                size: Theme.iconSizeSmall - 2
                                color: modeButton.active ? Theme.text : Theme.textMuted
                            }
                            Text {
                                id: modeLabel
                                anchors.verticalCenter: parent.verticalCenter
                                text: modeButton.modelData
                                color: modeButton.active ? Theme.text : Theme.textMuted
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                                font.weight: modeButton.active ? Font.DemiBold : Font.Medium
                            }
                        }
                        MouseArea {
                            id: modeArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: root.launcher.mode = modeButton.index
                        }
                    }
                }
            }
            Text {
                objectName: "runCount"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                visible: list.count > 0
                text: (root.launcher.selected + 1) + "/" + list.count
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                font.features: { "tnum": 1 }
            }
        }

        // The line to type in, after the cat in a bubble of the blush: its face pops as it
        // changes.
        Item {
            id: prompt
            x: root.padding; y: modes.y + modes.height + Theme.spacingM
            width: card.width - 2 * root.padding
            height: Theme.fieldHeight
            Rectangle {
                id: bubble
                objectName: "runFace"
                property alias text: faceText.text
                anchors.verticalCenter: parent.verticalCenter
                width: faceText.implicitWidth + 2 * Theme.spacingL
                height: Theme.fieldHeight - 2 * Theme.spacingS
                radius: height / 2
                color: Theme.alpha(Theme.blush, Theme.light ? 0.22 : 0.18)
                Text {
                    id: faceText
                    anchors.centerIn: parent
                    text: root.face
                    textFormat: Text.PlainText
                    color: Theme.light ? Theme.mix(Theme.blush, Theme.text, 0.45) : Theme.blush
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.Medium
                    onTextChanged: if (root.shown) pop.restart()
                }
                SequentialAnimation {
                    id: pop
                    NumberAnimation { target: bubble; property: "scale"; to: 1.12; duration: Theme.duration(70); easing.type: Easing.OutQuad }
                    NumberAnimation { target: bubble; property: "scale"; to: 1; duration: Theme.duration(160); easing.type: Easing.OutBack }
                }
            }
            TextField {
                id: input
                objectName: "runInput"
                anchors.left: bubble.right; anchors.leftMargin: Theme.spacingM
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                padding: 0; leftPadding: Theme.spacingXS
                background: Item {}
                color: Theme.text
                placeholderText: root.placeholders[root.launcher.mode]
                placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle
                onTextChanged: root.launcher.query = text
                Keys.onPressed: (event) => root.key(event)
            }
        }
        Rectangle {
            id: rule
            x: root.padding; y: prompt.y + prompt.height + Theme.spacingS
            width: card.width - 2 * root.padding; height: 1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.5) }
                GradientStop { position: 0.5; color: Theme.divider }
                GradientStop { position: 1; color: Theme.alpha(Theme.blush, 0.5) }
            }
        }

        ListView {
            id: list
            objectName: "runList"
            x: Theme.spacingM; y: rule.y + rule.height + Theme.spacingS
            width: card.width - 2 * x
            height: root.visibleRows * root.rowHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.launcher.results
            currentIndex: root.launcher.selected
            // The selection glides as quickly as the keys go, a dot of the blush at its start.
            highlightMoveDuration: Theme.durationFast
            highlightMoveVelocity: -1
            highlightResizeDuration: 0
            highlight: Rectangle {
                radius: Theme.radiusSmall
                color: Theme.accentSubtle
                Rectangle {
                    x: Theme.spacingXS + 1
                    anchors.verticalCenter: parent.verticalCenter
                    width: 3; height: parent.height * 0.46
                    radius: width / 2
                    color: Theme.blush
                }
            }
            // A new row eases in from the side; the others glide to their new places.
            add: Transition {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
                NumberAnimation { property: "x"; from: Theme.spacingL; to: 0; duration: Theme.durationNormal; easing.type: Theme.easing }
            }
            displaced: Transition {
                NumberAnimation { properties: "x,y"; duration: Theme.durationFast; easing.type: Theme.easing }
                NumberAnimation { property: "opacity"; to: 1; duration: Theme.durationFast }
            }
            move: Transition {
                NumberAnimation { property: "y"; duration: Theme.durationFast; easing.type: Theme.easing }
            }
            delegate: Item {
                id: row
                required property int index
                required property string kind
                required property string title
                required property string subtitle
                required property string icon
                required property var matches
                required property bool current
                objectName: "runRow:" + title
                width: list.width; height: root.rowHeight
                // As the launcher opens, the rows come in one after another.
                function hop() { hopping.restart() }
                Connections {
                    target: root
                    function onOpened() { arriving.restart() }
                }
                SequentialAnimation {
                    id: arriving
                    PropertyAction { target: content; property: "opacity"; value: Theme.durationNormal > 0 ? 0 : 1 }
                    PauseAnimation { duration: Theme.duration(Math.min(row.index, 8) * 18) }
                    ParallelAnimation {
                        NumberAnimation { target: content; property: "opacity"; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
                        NumberAnimation { target: content; property: "x"; from: Theme.spacingM; to: 0; duration: Theme.durationNormal; easing.type: Theme.easing }
                    }
                }
                SequentialAnimation {
                    id: hopping
                    NumberAnimation { target: content; property: "y"; to: -Theme.spacingS - 1; duration: Theme.duration(90); easing.type: Easing.OutQuad }
                    NumberAnimation { target: content; property: "y"; to: 0; duration: Theme.duration(140); easing.type: Easing.OutBounce }
                }
                Item {
                    id: content
                    width: parent.width; height: parent.height
                    Image {
                        id: glyph
                        x: Theme.spacingL; anchors.verticalCenter: parent.verticalCenter
                        width: Theme.appIconSize; height: width
                        sourceSize: Qt.size(2 * width, 2 * height)
                        source: shell.iconBase + row.icon
                    }
                    Text {
                        id: name
                        anchors.left: glyph.right; anchors.leftMargin: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, row.width - glyph.width - 4 * Theme.spacingL - Math.min(detail.implicitWidth, row.width * 0.4))
                        text: root.launcher.markup(row.title, row.matches, Theme.accent)
                        textFormat: Text.StyledText
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize + 1
                    }
                    Text {
                        id: detail
                        anchors.left: name.right; anchors.leftMargin: Theme.spacingM
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.current ? row.subtitle + " · focused" : row.subtitle
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onPositionChanged: (mouse) => {
                        var at = mapToItem(root, mouse.x, mouse.y)
                        var moved = root.pointerSeen && (at.x !== root.pointer.x || at.y !== root.pointer.y)
                        root.pointer = at
                        root.pointerSeen = true
                        if (moved)
                            root.launcher.selected = row.index
                    }
                    onClicked: { root.launcher.selected = row.index; root.run("chosen") }
                }
            }
        }
        // What nothing found says: what Enter does with the text instead.
        Column {
            objectName: "runEmpty"
            visible: list.count === 0
            anchors.horizontalCenter: list.horizontalCenter
            y: list.y + (list.height - height) / 2
            width: list.width - 2 * Theme.spacingXL
            spacing: Theme.spacingS
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: root.launcher.mode === 2 ? "No window by that name" : root.query === "" ? "Nothing here yet" : "Nothing found"
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.Medium
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                visible: root.launcher.mode !== 2 && root.query !== ""
                text: "Enter runs “" + root.query + "” as a command"
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
            }
        }

        // The keys, or why what was asked did not run.
        Item {
            id: footer
            x: root.padding; y: list.y + list.height + Theme.spacingS
            width: card.width - 2 * root.padding
            height: 22
            Row {
                visible: root.launcher.problem === ""
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingL
                Repeater {
                    model: root.launcher.mode === 2
                           ? [{ keys: "Enter", what: "go to it" }, { keys: "Tab", what: "mode" }, { keys: "Esc", what: "close" }]
                           : [{ keys: "Enter", what: "run" }, { keys: "Ctrl Enter", what: "as typed" },
                              { keys: "Shift Enter", what: "in a terminal" }, { keys: "Tab", what: "mode" }]
                    delegate: Row {
                        required property var modelData
                        spacing: Theme.spacingS
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: cap.implicitWidth + 2 * Theme.spacingS; height: cap.implicitHeight + 2
                            radius: Theme.radiusSmall - 2
                            color: Theme.surfaceRaised
                            border.color: Theme.border
                            Text {
                                id: cap
                                anchors.centerIn: parent
                                text: modelData.keys
                                color: Theme.textMuted
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.Medium
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.what
                            color: Theme.textMuted
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                        }
                    }
                }
            }
            Text {
                objectName: "runProblem"
                visible: root.launcher.problem !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                text: root.launcher.problem
                textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.danger
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
            }
        }
    }
}
