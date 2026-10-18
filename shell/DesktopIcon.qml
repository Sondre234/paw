// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// One icon on the desktop (DesktopIconGrid): its picture over its name, in a cell of the board. A
// picture file shows itself, as a print with a white border set a little askew; anything else
// shows its icon. The name sits on a chip of the bar's colour, so that it reads on any wallpaper,
// filled with the accent while selected, when it also shows in full. Hovered, it lifts a little.
// Dragged, it follows the pointer, and the others dragged with it trail after it into a fanned
// stack; let go, they glide into their new cells. A drag the compositor carries leaves it in its
// cell, faded (`lifted`), until it comes out of the stack where it was let go. A folder with a drag
// over it opens up, ringed in the second hue, wiggling, and gulps down what is dropped on it. While
// its file is still being copied in, a glow in the band's two hues breathes behind it. It comes and goes in a ripple as the grid's
// icons are shown and hidden, and a new file pops in. Renamed, its chip pops into a field on the
// surface's colour, rimmed in the band's two hues, with a pencil on its corner, the name selected
// but for its extension; a name refused shakes it.
Item {
    id: tile
    // The grid it is on (DesktopIconGrid).
    required property Item board
    required property int index
    required property string key
    required property string name
    required property string icon
    required property string picture
    required property string kind
    required property string appId
    required property int column
    required property int row
    required property bool arriving
    objectName: "desktopIcon:" + key
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.description: "Double-click to open"

    readonly property bool selected: board.selection[key] === true
    readonly property bool dragged: board.dragging && selected
    // Its picture in the stack the compositor carries, it stays in its cell, faded.
    readonly property bool lifted: dragged && board.handedOver
    readonly property bool following: dragged && !board.handedOver
    // Its place in the dragged stack: 0 under the pointer, then as they were selected.
    readonly property int rank: dragged ? board.dragKeys.indexOf(key) : 0
    readonly property bool placed: column >= 0 && row >= 0
    // Whether it has been in a cell: from there on it glides to new ones, but it takes its first at once.
    property bool landed: false
    onPlacedChanged: if (placed) Qt.callLater(function() { tile.landed = true })
    readonly property bool hovered: pointer.containsMouse && !board.dragging && !board.banding && !editing
    // Whether its name is being typed (DesktopIconGrid.renaming).
    readonly property bool editing: board.renaming === key
    // How far it is in view: 0 hidden, 1 shown, past 1 for a moment as it pops in.
    property real reveal: 1
    // Going to the trash, or into a folder: it shrinks away before the file goes.
    property bool leaving: false
    // A folder with a drag over it, which takes in what is dropped (DesktopIconGrid.dropFolder).
    readonly property bool dropTarget: board.dropFolder === key

    width: board.cellWidth
    height: board.cellHeight
    visible: placed && reveal > 0.001
    z: dragged ? 100 - rank : editing ? 4 : hovered ? 2 : selected ? 1 : 0
    x: following ? board.dragX + Math.min(rank, 3) * 7 : board.cellX(column)
    y: following ? board.dragY - Math.min(rank, 3) * 5 : board.cellY(row)
    Behavior on x { enabled: tile.landed && !tile.board.snapping; NumberAnimation { duration: board.glideDuration(tile); easing.type: tile.dragged ? Easing.OutCubic : Easing.OutBack; easing.overshoot: 1.1 } }
    Behavior on y { enabled: tile.landed && !tile.board.snapping; NumberAnimation { duration: board.glideDuration(tile); easing.type: tile.dragged ? Easing.OutCubic : Easing.OutBack; easing.overshoot: 1.1 } }

    // Shown and hidden in a ripple from where the menu was opened.
    Connections {
        target: tile.board
        function onShownChanged() { revealing.restart() }
    }
    SequentialAnimation {
        id: revealing
        PauseAnimation { duration: tile.board.rippleDelay(tile) }
        NumberAnimation {
            target: tile; property: "reveal"; to: tile.board.shown ? 1 : 0
            duration: Theme.duration(tile.board.shown ? 420 : 220)
            easing.type: tile.board.shown ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 2
        }
    }
    Component.onCompleted: {
        // One that comes once the desktop has settled is a new file: it pops in.
        reveal = 0
        if (board.shown && board.settled)
            revealing.restart()
        else
            reveal = board.shown ? 1 : 0
    }

    // Opening: a squash and a bounce back.
    function boing() { squash.restart() }
    // Taking in what was dropped on it: a deeper squash.
    function gulp() { gulping.restart() }
    // Caught by the selection band: a little hop.
    function hop() { if (!board.dragging) hopping.restart() }
    onSelectedChanged: if (selected && board.banding) hop()

    Item {
        id: body
        anchors.fill: parent
        opacity: Math.min(1, tile.reveal) * (tile.leaving ? 0 : 1) * (0.35 + 0.65 * (1 - lift))
        scale: (0.55 + 0.45 * tile.reveal) * (tile.leaving ? 0.4 : 1) * press * (1 - 0.08 * lift)
        // The open squash and the hop multiply in here, and how far it is faded into the stack the
        // compositor carries.
        property real press: 1
        property real lift: tile.lifted ? 1 : 0
        Behavior on lift { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }
        transformOrigin: Item.Center
        rotation: tile.leaving ? 12 : tile.following ? [0, -7, 6, -4][Math.min(tile.rank, 3)] : 0
        Behavior on rotation { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutBack } }
        Behavior on opacity { enabled: tile.leaving; NumberAnimation { duration: Theme.duration(160); easing.type: Easing.InCubic } }
        Behavior on scale { enabled: tile.leaving; NumberAnimation { duration: Theme.duration(160); easing.type: Easing.InCubic } }

        // The plate behind it while hovered or selected.
        Rectangle {
            id: plate
            anchors.fill: parent
            anchors.margins: Theme.spacingXS
            radius: Theme.radiusLarge
            color: tile.dropTarget ? Theme.alpha(tile.board.partner, 0.24)
                 : tile.selected ? Theme.alpha(Theme.accent, tile.following ? 0 : 0.2)
                 : tile.hovered ? Theme.alpha(Theme.surface, 0.32) : "transparent"
            border.width: tile.dropTarget ? 2 : 1
            border.color: tile.dropTarget ? tile.board.partner
                        : tile.selected && !tile.following ? Theme.alpha(Theme.accent, 0.6)
                        : tile.hovered ? Theme.alpha(Theme.text, 0.12) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }

        // The glow while its file comes in, a soft halo round a brighter heart, turning slowly as
        // it breathes.
        Item {
            id: glow
            anchors.centerIn: art
            width: art.width * 1.3
            height: width
            visible: opacity > 0.01
            opacity: tile.arriving ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
            Repeater {
                model: [[1.3, 0.14], [1, 0.3], [0.72, 0.22]]
                delegate: Rectangle {
                    required property var modelData
                    anchors.centerIn: parent
                    width: glow.width * modelData[0]
                    height: width
                    radius: width / 2
                    gradient: Gradient {
                        GradientStop { position: 0; color: Theme.alpha(Theme.accent, modelData[1]) }
                        GradientStop { position: 1; color: Theme.alpha(tile.board.partner, modelData[1]) }
                    }
                }
            }
            SequentialAnimation on scale {
                running: glow.visible && Theme.duration(100) > 0
                loops: Animation.Infinite
                NumberAnimation { from: 0.8; to: 1.08; duration: Theme.duration(650); easing.type: Easing.InOutSine }
                NumberAnimation { to: 0.8; duration: Theme.duration(650); easing.type: Easing.InOutSine }
            }
            RotationAnimation on rotation {
                running: glow.visible && Theme.duration(100) > 0
                loops: Animation.Infinite
                from: 0; to: 360
                duration: Theme.duration(2600)
            }
        }
        DesktopIconArt {
            id: art
            width: tile.board.iconSize
            height: tile.board.iconSize
            x: Math.round((tile.width - width) / 2)
            y: Theme.spacingM + (tile.hovered ? -3 : 0)
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            icon: tile.icon
            picture: tile.picture
            name: tile.name
            // A folder with a drag over it opens up and wiggles, waiting for what it is to take.
            scale: tile.dropTarget ? 1.16 : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack; easing.overshoot: 2 } }
            SequentialAnimation on rotation {
                running: tile.dropTarget && Theme.duration(100) > 0
                loops: Animation.Infinite
                onRunningChanged: if (!running) art.rotation = 0
                NumberAnimation { to: -5; duration: Theme.duration(140); easing.type: Easing.InOutSine }
                NumberAnimation { to: 5; duration: Theme.duration(280); easing.type: Easing.InOutSine }
                NumberAnimation { to: 0; duration: Theme.duration(140); easing.type: Easing.InOutSine }
                PauseAnimation { duration: Theme.duration(260) }
            }
        }

        // The name on its chip.
        Rectangle {
            id: chip
            readonly property real widest: tile.width - 2 * Theme.spacingXS
            // A pixel or two to spare, as the text lays out a little wider than it measures; while
            // it is typed, what is typed and room for the caret, never narrower than a few letters.
            width: Math.min(widest, tile.editing
                ? Math.max(3 * Theme.spacingL, Math.ceil(typedMetrics.advanceWidth) + 2 * Theme.spacingM + 6)
                : Math.ceil(metrics.advanceWidth) + 2 * Theme.spacingM + 2)
            height: (field.item ? field.item.height : label.height) + 2 * Theme.spacingXS
            // Shaken from side to side as a name is refused.
            property real shake: 0
            x: Math.round((tile.width - width) / 2) + shake
            y: art.y + art.height + Theme.spacingS + (tile.hovered ? 3 : 0)
            radius: Math.min(height / 2, Theme.radiusMedium)
            color: tile.editing ? Theme.surface
                 : tile.selected ? Theme.accent : Theme.alpha(Theme.surface, Theme.light ? 0.8 : 0.72)
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            // In the dragged stack only the icon under the pointer keeps its name.
            opacity: tile.following && tile.rank > 0 ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            TextMetrics { id: metrics; font: label.font; text: tile.name }
            TextMetrics { id: typedMetrics; font: label.font; text: tile.editing ? tile.board.renameText : "" }
            // Its rim while the name is typed, from the accent into its partner, as the band's.
            Rectangle {
                z: -1
                visible: tile.editing
                anchors.fill: parent
                anchors.margins: -1.5
                radius: parent.radius + 1.5
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.accent }
                    GradientStop { position: 1; color: tile.board.partner }
                }
            }
            Text {
                id: label
                visible: !tile.editing
                x: Theme.spacingM
                y: Theme.spacingXS
                width: chip.width - 2 * Theme.spacingM
                text: tile.name
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: tile.selected && !tile.following ? 5 : 2
                elide: Text.ElideRight
                color: tile.selected ? Theme.textOnAccent : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
            }
            // The field the name is typed in, which takes the keyboard: Enter gives the name,
            // Escape keeps the old one, and losing the keyboard or a press elsewhere gives it too.
            Loader {
                id: field
                active: tile.editing
                x: Theme.spacingM
                y: Theme.spacingXS
                sourceComponent: TextInput {
                    id: input
                    objectName: "desktopRenameField"
                    width: chip.width - 2 * Theme.spacingM
                    color: Theme.text
                    selectionColor: Theme.alpha(tile.board.partner, 0.45)
                    selectedTextColor: Theme.text
                    font: label.font
                    horizontalAlignment: TextInput.AlignHCenter
                    wrapMode: TextInput.WrapAtWordBoundaryOrAnywhere
                    selectByMouse: true
                    // A caret in the second hue, blinking for a while after each change; drawn left
                    // of where the field puts it, so that it hides no narrow letter (a dot).
                    cursorDelegate: Item {
                        width: 2
                        visible: input.activeFocus && input.selectionStart === input.selectionEnd && caret.lit
                        Rectangle {
                            x: -1
                            width: 2
                            height: parent.height
                            radius: 1
                            color: tile.board.partner
                        }
                    }
                    QtObject {
                        id: caret
                        property bool lit: true
                        property int blinks: 0
                    }
                    Timer {
                        interval: 530
                        repeat: true
                        running: input.activeFocus && caret.blinks < 20
                        onTriggered: { caret.lit = !caret.lit; ++caret.blinks }
                    }
                    function woken() { caret.lit = true; caret.blinks = 0 }
                    onCursorPositionChanged: woken()
                    onTextChanged: { woken(); tile.board.renameText = text }
                    // The name selected but for its extension (a file's after its last dot).
                    Component.onCompleted: {
                        text = tile.board.renameText
                        const dot = tile.kind === "file" ? text.lastIndexOf(".") : -1
                        select(0, dot > 0 ? dot : text.length)
                        forceActiveFocus()
                    }
                    onActiveFocusChanged: if (!activeFocus) tile.board.finishRename(text, false)
                    Keys.onReturnPressed: tile.board.finishRename(text, true)
                    Keys.onEnterPressed: tile.board.finishRename(text, true)
                    Keys.onTabPressed: tile.board.finishRename(text, false)
                    Keys.onEscapePressed: tile.board.finishRename(undefined)
                }
            }
            // A pencil on its corner while the name is typed, clear of the name.
            Rectangle {
                visible: tile.editing
                width: 18
                height: 18
                radius: 9
                x: chip.width - width / 2 - 2
                y: -height / 2 - 1
                color: tile.board.partner
                border.color: Theme.surface
                border.width: 2
                Icon {
                    anchors.centerIn: parent
                    name: "pencil"
                    size: 10
                    color: Theme.contrast(tile.board.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                }
            }
        }

        // How many go with it, on the icon under the pointer.
        Rectangle {
            visible: tile.following && tile.rank === 0 && tile.board.dragKeys.length > 1
            x: art.x + art.width - width / 2
            y: art.y - height / 3
            height: 20
            width: Math.max(height, count.implicitWidth + 2 * Theme.spacingM)
            radius: height / 2
            color: tile.board.partner
            border.color: Theme.surface
            border.width: 2
            Text {
                id: count
                anchors.centerIn: parent
                text: tile.board.dragKeys.length
                color: Theme.contrast(tile.board.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCaption
                font.bold: true
            }
        }
    }

    SequentialAnimation {
        id: gulping
        NumberAnimation { target: body; property: "press"; to: 0.74; duration: Theme.duration(110); easing.type: Easing.OutQuad }
        NumberAnimation { target: body; property: "press"; to: 1; duration: Theme.duration(520); easing.type: Easing.OutElastic; easing.amplitude: 1.3; easing.period: 0.4 }
    }
    SequentialAnimation {
        id: squash
        NumberAnimation { target: body; property: "press"; to: 0.86; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: body; property: "press"; to: 1; duration: Theme.duration(360); easing.type: Easing.OutElastic; easing.amplitude: 1.2; easing.period: 0.45 }
    }
    // The chip popping into a field, and shaken as a name is refused.
    onEditingChanged: if (editing) popping.restart()
    SequentialAnimation {
        id: popping
        NumberAnimation { target: chip; property: "scale"; to: 1.08; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: chip; property: "scale"; to: 1; duration: Theme.duration(240); easing.type: Easing.OutBack }
    }
    Connections {
        target: tile.board
        function onRenameRefused() { if (tile.editing) shaking.restart() }
    }
    SequentialAnimation {
        id: shaking
        NumberAnimation { target: chip; property: "shake"; to: -6; duration: Theme.duration(45) }
        NumberAnimation { target: chip; property: "shake"; to: 6; duration: Theme.duration(70) }
        NumberAnimation { target: chip; property: "shake"; to: -4; duration: Theme.duration(65) }
        NumberAnimation { target: chip; property: "shake"; to: 3; duration: Theme.duration(60) }
        NumberAnimation { target: chip; property: "shake"; to: 0; duration: Theme.duration(55) }
    }
    SequentialAnimation {
        id: hopping
        NumberAnimation { target: body; property: "press"; to: 1.1; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: body; property: "press"; to: 1; duration: Theme.duration(200); easing.type: Easing.OutBack }
    }

    MouseArea {
        id: pointer
        // Under the icon, which takes no presses but on the field its name is typed in.
        z: -1
        anchors.fill: parent
        anchors.margins: Theme.spacingXS
        // Not while the compositor carries a drag, as Qt never hears the button let go then.
        enabled: tile.board.shown && !tile.leaving && !tile.board.carried
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        hoverEnabled: true
        // Where the left button went down, in the grid's coordinates, whether that may drag, and
        // whether it has moved far enough since to; and whether the click takes the icon alone.
        property point pressedAt
        property bool armed: false
        property bool moved: false
        property bool alone: true
        onPressed: function(mouse) {
            const at = mapToItem(tile.board, mouse.x, mouse.y)
            const board = tile.board
            board.pressed()
            board.keysUsed = false
            armed = false
            board.current = tile.key
            // The middle button, or Ctrl with the left, picks an icon or puts it back, keeping the
            // others selected.
            if (mouse.button === Qt.MiddleButton ||
                (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ControlModifier))) {
                board.toggle(tile.key)
                board.extendFrom = tile.key
                return
            }
            // Shift with the left takes those from the icon clicked before to this one.
            alone = !(mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier))
            if (!alone) {
                board.selectBetween(board.extendFrom !== "" && board.tileOf(board.extendFrom) ? board.extendFrom : tile.key,
                                    tile.key)
            } else if (!tile.selected) {
                board.select([tile.key])
                board.extendFrom = tile.key
            }
            if (mouse.button === Qt.RightButton) {
                board.iconMenuRequested(at.x, at.y, false)
                return
            }
            pressedAt = at
            armed = true
            moved = false
        }
        onPositionChanged: function(mouse) {
            if (!armed || !(pressedButtons & Qt.LeftButton))
                return
            const at = mapToItem(tile.board, mouse.x, mouse.y)
            if (!moved && Math.hypot(at.x - pressedAt.x, at.y - pressedAt.y) >= Qt.styleHints.startDragDistance) {
                moved = true
                tile.board.beginDrag(tile, pressedAt)
            }
            if (moved)
                tile.board.dragTo(at)
        }
        onReleased: function(mouse) {
            if (mouse.button !== Qt.LeftButton)
                return
            if (moved) {
                if (!tile.board.carried)
                    tile.board.endDrag(false)
            } else if (armed && alone) {
                tile.board.select([tile.key]) // a click on one of several takes it alone
                tile.board.extendFrom = tile.key
            }
            moved = armed = false
        }
        onCanceled: {
            if (moved && !tile.board.carried)
                tile.board.endDrag(true)
            moved = armed = false
        }
        onDoubleClicked: function(mouse) {
            if (mouse.button === Qt.LeftButton &&
                !(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                tile.board.open([tile])
        }
    }
}
