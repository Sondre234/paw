// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// One icon on the desktop (DesktopIconGrid): its picture over its name, in a cell of the board. A
// picture file shows itself, as a print with a white border set a little askew; anything else
// shows its icon. The name sits on a chip of the bar's colour, so that it reads on any wallpaper,
// filled with the accent while selected, when it also shows in full. Hovered, it lifts a little.
// Dragged, it follows the pointer, and the others dragged with it trail after it into a fanned
// stack; let go, they glide into their new cells. It comes and goes in a ripple as the grid's
// icons are shown and hidden, and a new file pops in.
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
    objectName: "desktopIcon:" + key
    Accessible.role: Accessible.Button
    Accessible.name: name
    Accessible.description: "Double-click to open"

    readonly property bool selected: board.selection[key] === true
    readonly property bool dragged: board.dragging && selected
    // Its place in the dragged stack: 0 under the pointer, then as they were selected.
    readonly property int rank: dragged ? board.dragKeys.indexOf(key) : 0
    readonly property bool placed: column >= 0 && row >= 0
    // Whether it has been in a cell: from there on it glides to new ones, but it takes its first at once.
    property bool landed: false
    onPlacedChanged: if (placed) Qt.callLater(function() { tile.landed = true })
    readonly property bool hovered: pointer.containsMouse && !board.dragging && !board.banding
    // How far it is in view: 0 hidden, 1 shown, past 1 for a moment as it pops in.
    property real reveal: 1
    // Going to the trash: it shrinks away before the file goes.
    property bool leaving: false

    width: board.cellWidth
    height: board.cellHeight
    visible: placed && reveal > 0.001
    z: dragged ? 100 - rank : hovered ? 2 : selected ? 1 : 0
    x: dragged ? board.dragX + Math.min(rank, 3) * 7 : board.cellX(column)
    y: dragged ? board.dragY - Math.min(rank, 3) * 5 : board.cellY(row)
    Behavior on x { enabled: tile.landed; NumberAnimation { duration: board.glideDuration(tile); easing.type: tile.dragged ? Easing.OutCubic : Easing.OutBack; easing.overshoot: 1.1 } }
    Behavior on y { enabled: tile.landed; NumberAnimation { duration: board.glideDuration(tile); easing.type: tile.dragged ? Easing.OutCubic : Easing.OutBack; easing.overshoot: 1.1 } }

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
    // Caught by the selection band: a little hop.
    function hop() { if (!board.dragging) hopping.restart() }
    onSelectedChanged: if (selected && board.banding) hop()

    Item {
        id: body
        anchors.fill: parent
        opacity: Math.min(1, tile.reveal) * (tile.leaving ? 0 : 1)
        scale: (0.55 + 0.45 * tile.reveal) * (tile.leaving ? 0.4 : 1) * press
        // The open squash and the hop multiply in here.
        property real press: 1
        transformOrigin: Item.Center
        rotation: tile.leaving ? 12 : tile.dragged ? [0, -7, 6, -4][Math.min(tile.rank, 3)] : 0
        Behavior on rotation { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutBack } }
        Behavior on opacity { enabled: tile.leaving; NumberAnimation { duration: Theme.duration(160); easing.type: Easing.InCubic } }
        Behavior on scale { enabled: tile.leaving; NumberAnimation { duration: Theme.duration(160); easing.type: Easing.InCubic } }

        // The plate behind it while hovered or selected.
        Rectangle {
            id: plate
            anchors.fill: parent
            anchors.margins: Theme.spacingXS
            radius: Theme.radiusLarge
            color: tile.selected ? Theme.alpha(Theme.accent, tile.dragged ? 0 : 0.2)
                 : tile.hovered ? Theme.alpha(Theme.surface, 0.32) : "transparent"
            border.width: 1
            border.color: tile.selected && !tile.dragged ? Theme.alpha(Theme.accent, 0.6)
                        : tile.hovered ? Theme.alpha(Theme.text, 0.12) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }

        Item {
            id: art
            width: tile.board.iconSize
            height: tile.board.iconSize
            x: Math.round((tile.width - width) / 2)
            y: Theme.spacingM + (tile.hovered ? -3 : 0)
            Behavior on y { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }

            Image {
                anchors.fill: parent
                visible: tile.picture === ""
                source: tile.picture === "" && tile.icon !== "" ? shell.iconBase + tile.icon : ""
                sourceSize: Qt.size(width, height)
                smooth: true
            }
            // A picture as a print: a white border, wider at the foot, set askew by its name.
            Rectangle {
                id: snapshot
                visible: tile.picture !== ""
                anchors.centerIn: parent
                width: Math.round(parent.width * 0.86)
                height: Math.round(width * 1.12)
                color: "#fbfaf6"
                radius: 2
                rotation: [-5, 3, -2, 4, -3, 2][(tile.name.length + tile.name.charCodeAt(0)) % 6]
                antialiasing: true
                // Its shadow on the desktop.
                Rectangle {
                    z: -1
                    x: 1.5; y: 2.5
                    width: parent.width; height: parent.height
                    radius: parent.radius
                    color: Qt.rgba(0, 0, 0, 0.22)
                }
                Image {
                    x: 3; y: 3
                    width: parent.width - 6
                    height: parent.height - 3 - Math.round(parent.height * 0.16)
                    source: tile.picture
                    sourceSize: Qt.size(width * 2, height * 2)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    smooth: true
                }
            }
        }

        // The name on its chip.
        Rectangle {
            id: chip
            readonly property real widest: tile.width - 2 * Theme.spacingXS
            // A pixel or two to spare, as the text lays out a little wider than it measures.
            width: Math.min(widest, Math.ceil(metrics.advanceWidth) + 2 * Theme.spacingM + 2)
            height: label.height + 2 * Theme.spacingXS
            x: Math.round((tile.width - width) / 2)
            y: art.y + art.height + Theme.spacingS + (tile.hovered ? 3 : 0)
            radius: Math.min(height / 2, Theme.radiusMedium)
            color: tile.selected ? Theme.accent : Theme.alpha(Theme.surface, Theme.light ? 0.8 : 0.72)
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            // In the dragged stack only the icon under the pointer keeps its name.
            opacity: tile.dragged && tile.rank > 0 ? 0 : 1
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
            TextMetrics { id: metrics; font: label.font; text: tile.name }
            Text {
                id: label
                x: Theme.spacingM
                y: Theme.spacingXS
                width: chip.width - 2 * Theme.spacingM
                text: tile.name
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: tile.selected && !tile.dragged ? 5 : 2
                elide: Text.ElideRight
                color: tile.selected ? Theme.textOnAccent : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        // How many go with it, on the icon under the pointer.
        Rectangle {
            visible: tile.dragged && tile.rank === 0 && tile.board.dragKeys.length > 1
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
        id: squash
        NumberAnimation { target: body; property: "press"; to: 0.86; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: body; property: "press"; to: 1; duration: Theme.duration(360); easing.type: Easing.OutElastic; easing.amplitude: 1.2; easing.period: 0.45 }
    }
    SequentialAnimation {
        id: hopping
        NumberAnimation { target: body; property: "press"; to: 1.1; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: body; property: "press"; to: 1; duration: Theme.duration(200); easing.type: Easing.OutBack }
    }

    MouseArea {
        id: pointer
        anchors.fill: parent
        anchors.margins: Theme.spacingXS
        enabled: tile.board.shown && !tile.leaving
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
                tile.board.endDrag(false)
            } else if (armed && alone) {
                tile.board.select([tile.key]) // a click on one of several takes it alone
                tile.board.extendFrom = tile.key
            }
            moved = armed = false
        }
        onCanceled: {
            if (moved)
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
