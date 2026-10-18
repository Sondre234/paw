// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// One sticky note on the desktop (DesktopIconGrid, the model shell.desktopNotes): pastel paper over
// a block of the board's cells, set a little askew by its number as the icons' prints are, held on
// by a strip of washi tape across its top, with a soft shadow and, in its corner, a doodle that
// goes with its colour: a cat's face on sakura, a cloud on sky, a sprout on mint, a star on lemon,
// a heart on lavender. Its text is in a rounded hand (the board's noteFont); a line that starts
// with "[ ]" or "- [ ]" is a checklist's, its box ticked or cleared by a click, its text struck
// through once ticked; the first of several lines is its title. A click writes on it: it lies
// straight and lifts, a pencil on its corner, its text plain in a field (`editing`), saying near
// the end how many more characters it holds. Dragged, it swings further askew under the pointer,
// and let go it glides into the block of cells the board's pad showed. Deleted, it twists and
// crumples away. Hovered, it lifts a little and its doodle wiggles. It comes and goes in the icons'
// ripple, and pops in as it is made.
Item {
    id: note
    // The grid it is on (DesktopIconGrid).
    required property Item board
    required property int index
    required property string key
    required property int noteId
    required property string text
    required property string color
    required property int column
    required property int row
    objectName: "desktopNote:" + key
    Accessible.role: Accessible.Note
    Accessible.name: name !== "" ? "Note: " + name : "Empty note"
    Accessible.description: color + " sticky note"

    readonly property bool isNote: true
    // How many cells it covers, across and down.
    readonly property int spanColumns: board.notes.columns
    readonly property int spanRows: board.notes.rows
    readonly property bool placed: column >= 0 && row >= 0
    // Its first line, which names it, without a checklist's box.
    readonly property string name: {
        const first = board.notes.lines(text)[0]
        return first ? first.text.trim() : ""
    }
    // Its lines as they show (DesktopNotes.lines).
    readonly property var lines: board.notes.lines(text)
    readonly property color paperColor: Theme.notePaper(color)
    readonly property color ink: Theme.noteInk(color)
    // How askew it is set, and where its tape is stuck, by its number.
    readonly property real tilt: [-2.2, 1.6, -1.1, 2.4, -1.7, 1.2][noteId % 6]
    readonly property bool hovered: hover.hovered && !board.dragging && !board.banding && !editing && board.noteDrag === ""
    // Whether it follows the pointer, dragged (DesktopIconGrid.noteDrag).
    readonly property bool dragged: board.noteDrag === key
    // Whether its text is being typed (DesktopIconGrid.editingNote).
    readonly property bool editing: board.editingNote === key
    // Whether it has been in a cell: from there on it glides to new ones.
    property bool landed: false
    onPlacedChanged: if (placed) Qt.callLater(function() { note.landed = true })
    // How far it is in view: 0 hidden, 1 shown, past 1 for a moment as it pops in.
    property real reveal: 1
    // Deleted: it crumples away before it goes.
    property bool leaving: false

    width: board.cellWidth * spanColumns
    height: board.cellHeight * spanRows
    visible: placed && reveal > 0.001
    z: dragged ? 110 : editing ? 5 : hovered ? 2 : 0
    x: dragged ? board.noteDragX : board.cellX(column)
    y: dragged ? board.noteDragY : board.cellY(row)
    Behavior on x { enabled: note.landed && !note.dragged; NumberAnimation { duration: Theme.duration(380); easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
    Behavior on y { enabled: note.landed && !note.dragged; NumberAnimation { duration: Theme.duration(380); easing.type: Easing.OutBack; easing.overshoot: 1.1 } }

    HoverHandler { id: hover }

    // Shown and hidden in the icons' ripple, from where the menu was opened.
    Connections {
        target: note.board
        function onShownChanged() { revealing.restart() }
    }
    SequentialAnimation {
        id: revealing
        PauseAnimation { duration: note.board.rippleDelay(note) }
        NumberAnimation {
            target: note; property: "reveal"; to: note.board.shown ? 1 : 0
            duration: Theme.duration(note.board.shown ? 460 : 220)
            easing.type: note.board.shown ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 1.8
        }
    }
    Component.onCompleted: {
        // One made once the desktop has settled pops in.
        reveal = 0
        if (board.shown && board.settled)
            revealing.restart()
        else
            reveal = board.shown ? 1 : 0
    }
    function popIn() {
        reveal = 0
        revealing.restart()
    }
    // A little hop, as its text is copied or its colour changes.
    function hop() { hopping.restart() }
    SequentialAnimation {
        id: hopping
        NumberAnimation { target: sheet; property: "press"; to: 1.06; duration: Theme.duration(90); easing.type: Easing.OutQuad }
        NumberAnimation { target: sheet; property: "press"; to: 1; duration: Theme.duration(300); easing.type: Easing.OutBack; easing.overshoot: 2 }
    }

    // The note, inside the block with room for its tape above it and its shadow around it.
    Item {
        id: sheet
        x: Theme.spacingM
        y: Theme.spacingM + 6
        width: note.width - 2 * Theme.spacingM
        height: note.height - y - Theme.spacingM - 2
        opacity: Math.min(1, note.reveal) * (note.leaving ? 0 : 1)
        scale: (0.6 + 0.4 * note.reveal) * press * (note.leaving ? 0.3
               : note.dragged ? 1.05 : note.editing ? 1.02 : note.hovered ? 1.015 : 1)
        // The hop multiplies in here.
        property real press: 1
        // Written on, it lies straight; picked up, it swings further askew; deleted, it twists away.
        rotation: note.leaving ? note.tilt + 24 : note.dragged ? note.tilt * 2 + (note.tilt < 0 ? -2 : 2)
                : note.editing ? 0 : note.hovered ? note.tilt * 0.5 : note.tilt
        Behavior on rotation { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutBack } }
        Behavior on scale { enabled: note.reveal === 1 && !hopping.running; NumberAnimation { duration: note.leaving ? Theme.duration(200) : Theme.durationNormal; easing.type: note.leaving ? Easing.InBack : Theme.easing } }
        Behavior on opacity { enabled: note.leaving; NumberAnimation { duration: Theme.duration(200); easing.type: Easing.InCubic } }
        antialiasing: true

        // A click writes on it, the caret where it was; a right press opens its menu; dragged, by
        // the paper or by its tape while it is written on, it moves. Under what it holds, which
        // takes its own presses (a box, the field), and over its tape too.
        MouseArea {
            id: pointer
            anchors.fill: parent
            anchors.topMargin: -12
            enabled: note.board.shown && !note.leaving
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            // Where the left button went down, in the board's coordinates, and whether it has
            // moved far enough since to drag.
            property point pressedAt
            property bool armed: false
            property bool moved: false
            onPressed: function(mouse) {
                note.board.notePressed(note.key)
                armed = mouse.button === Qt.LeftButton
                moved = false
                pressedAt = mapToItem(note.board, mouse.x, mouse.y)
                if (mouse.button === Qt.RightButton)
                    note.board.noteMenuRequested(note.key, pressedAt.x, pressedAt.y, false)
            }
            onPositionChanged: function(mouse) {
                if (!armed || !(pressedButtons & Qt.LeftButton))
                    return
                const at = mapToItem(note.board, mouse.x, mouse.y)
                if (!moved && Math.hypot(at.x - pressedAt.x, at.y - pressedAt.y) >= Qt.styleHints.startDragDistance) {
                    moved = true
                    note.board.beginNoteDrag(note, pressedAt)
                }
                if (moved)
                    note.board.noteDragTo(at)
            }
            onReleased: function(mouse) {
                if (mouse.button !== Qt.LeftButton)
                    return
                if (moved)
                    note.board.endNoteDrag(false)
                else if (armed && !note.editing)
                    note.board.startEditing(note.key, mapToItem(page, mouse.x, mouse.y))
                moved = armed = false
            }
            onCanceled: {
                if (moved)
                    note.board.endNoteDrag(true)
                moved = armed = false
            }
        }

        // Its shadow: through the GPU a soft one, deeper as it lifts; else two soft layers.
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: Shadow {
                radius: paper.radius
                blur: note.dragged ? 28 : note.hovered || note.editing ? 18 : 12
                offset: Qt.vector2d(0, note.dragged ? 14 : note.hovered || note.editing ? 8 : 5)
                color: Qt.rgba(0, 0, 0, Theme.light ? 0.2 : 0.5)
                Behavior on blur { NumberAnimation { duration: Theme.durationNormal } }
            }
        }
        Repeater {
            model: Theme.effects ? 0 : 2
            delegate: Rectangle {
                required property int index
                x: 1 + 1.5 * index
                y: 3 + 2.5 * index
                width: sheet.width
                height: sheet.height
                radius: paper.radius + 2 * index
                color: Qt.rgba(0, 0, 0, (Theme.light ? 0.09 : 0.28) / (index + 1))
            }
        }

        Rectangle {
            id: paper
            anchors.fill: parent
            radius: 5
            color: note.paperColor
            Behavior on color { ColorAnimation { duration: Theme.durationSlow } }
            border.width: 1
            border.color: Theme.alpha(note.ink, Theme.light ? 0.08 : 0.14)
            antialiasing: true
            // A sheen down from its top, and its foot a shade deeper, as paper catches the light.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.4 : 0.07) }
                    GradientStop { position: 0.4; color: Qt.rgba(1, 1, 1, 0) }
                    GradientStop { position: 1; color: Qt.rgba(0, 0, 0, Theme.light ? 0.05 : 0.12) }
                }
            }
        }

        // What it says: its lines, a checklist's with their boxes, faded out at the foot while
        // there is more than shows.
        Item {
            id: page
            x: 14
            y: 18
            width: paper.width - 2 * x
            height: paper.height - y - 30
            clip: true
            Column {
                id: shown
                visible: !note.editing
                width: parent.width
                spacing: 2
                Repeater {
                    model: note.editing ? [] : note.lines
                    delegate: Item {
                        id: line
                        required property var modelData
                        required property int index
                        width: shown.width
                        height: Math.max(words.implicitHeight, modelData.box ? box.height + 4 : 0)
                        Rectangle {
                            id: box
                            objectName: line.modelData.box ? "desktopNoteBox" : ""
                            visible: line.modelData.box
                            x: line.modelData.indent * 4
                            y: Math.max(0, (noteFont.height - height) / 2)
                            width: 14
                            height: 14
                            radius: 4
                            color: line.modelData.checked ? Theme.alpha(note.ink, 0.78) : Qt.rgba(1, 1, 1, Theme.light ? 0.45 : 0.08)
                            border.width: 1.5
                            border.color: Theme.alpha(note.ink, 0.7)
                            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                            Accessible.role: Accessible.CheckBox
                            Accessible.name: line.modelData.text
                            Accessible.checkable: true
                            Accessible.checked: line.modelData.checked
                            Icon {
                                visible: line.modelData.checked
                                anchors.centerIn: parent
                                name: "check"
                                size: 11
                                color: note.paperColor
                            }
                            // Ticked or cleared by a click, with a little hop.
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                enabled: note.board.shown
                                onPressed: note.board.pressed()
                                onClicked: {
                                    note.board.notes.toggle(note.key, line.index)
                                    tick.restart()
                                }
                            }
                            SequentialAnimation {
                                id: tick
                                NumberAnimation { target: box; property: "scale"; to: 1.3; duration: Theme.duration(90); easing.type: Easing.OutQuad }
                                NumberAnimation { target: box; property: "scale"; to: 1; duration: Theme.duration(260); easing.type: Easing.OutBack; easing.overshoot: 2.5 }
                            }
                        }
                        Text {
                            id: words
                            x: line.modelData.box ? box.x + box.width + 6 : 0
                            width: parent.width - x
                            // An empty line keeps a line's height.
                            text: line.modelData.text !== "" ? line.modelData.text : " "
                            textFormat: Text.PlainText
                            wrapMode: Text.Wrap
                            color: note.ink
                            opacity: line.modelData.checked ? 0.5 : 1
                            font.strikeout: line.modelData.checked
                            // The first of several lines is its title.
                            font.weight: line.index === 0 && note.lines.length > 1 && !line.modelData.box ? Font.DemiBold : Font.Normal
                            font.family: note.board.noteFont
                            font.pixelSize: note.board.noteFontSize
                        }
                    }
                }
            }
            FontMetrics { id: noteFont; font.family: note.board.noteFont; font.pixelSize: note.board.noteFontSize }
            Text {
                visible: note.text === ""
                text: note.editing ? "Write something…" : "Empty note"
                color: note.ink
                opacity: 0.45
                font.family: note.board.noteFont
                font.pixelSize: note.board.noteFontSize
            }
            // Its text to type, plain, lines broken by Enter, the caret kept in view; Escape, Tab, a
            // press elsewhere or the keyboard going to a window lets it go.
            Loader {
                id: editor
                anchors.fill: parent
                active: note.editing
                sourceComponent: Flickable {
                    id: flick
                    clip: true
                    contentWidth: width
                    contentHeight: field.contentHeight
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    function follow(caret) {
                        if (caret.y < contentY)
                            contentY = caret.y
                        else if (caret.y + caret.height > contentY + height)
                            contentY = caret.y + caret.height - height
                    }
                    TextEdit {
                        id: field
                        objectName: "desktopNoteField"
                        width: flick.width
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        color: note.ink
                        selectionColor: Theme.alpha(note.ink, 0.22)
                        selectedTextColor: note.ink
                        selectByMouse: true
                        font.family: note.board.noteFont
                        font.pixelSize: note.board.noteFontSize
                        Accessible.role: Accessible.EditableText
                        Accessible.name: "Note"
                        // A caret in the ink, blinking for a while after each change.
                        cursorDelegate: Rectangle {
                            width: 2
                            radius: 1
                            color: note.ink
                            visible: field.activeFocus && field.selectionStart === field.selectionEnd && caret.lit
                        }
                        QtObject {
                            id: caret
                            property bool lit: true
                            property int blinks: 0
                        }
                        Timer {
                            interval: 530
                            repeat: true
                            running: field.activeFocus && caret.blinks < 20
                            onTriggered: { caret.lit = !caret.lit; ++caret.blinks }
                        }
                        onCursorPositionChanged: { caret.lit = true; caret.blinks = 0 }
                        onCursorRectangleChanged: flick.follow(cursorRectangle)
                        Component.onCompleted: {
                            text = note.text
                            const at = note.board.editPoint
                            cursorPosition = at.x >= 0 ? positionAt(at.x, at.y) : length
                            forceActiveFocus()
                        }
                        // What is typed or pasted past the most a note holds is left out, and its
                        // count shakes.
                        onTextChanged: {
                            const most = note.board.notes.maxLength
                            if (length > most) {
                                remove(Math.max(0, cursorPosition - (length - most)), cursorPosition)
                                if (length > most)
                                    remove(most, length)
                                full.restart()
                                return
                            }
                            note.board.notes.setText(note.key, text)
                        }
                        onActiveFocusChanged: if (!activeFocus) note.board.finishEditing(note.key, false)
                        Keys.onEscapePressed: note.board.finishEditing(note.key, true)
                        Keys.onTabPressed: note.board.finishEditing(note.key, true)
                        Keys.onBacktabPressed: note.board.finishEditing(note.key, true)
                    }
                }
            }
            Rectangle {
                visible: !note.editing && shown.height > page.height
                anchors.bottom: parent.bottom
                width: parent.width
                height: 22
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.alpha(note.paperColor, 0) }
                    GradientStop { position: 1; color: note.paperColor }
                }
            }
        }

        // A pencil on its corner while it is written on, as on a name being typed, popping in.
        Rectangle {
            visible: scale > 0.01
            scale: note.editing ? 1 : 0
            Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutBack; easing.overshoot: 2 } }
            width: 22
            height: 22
            radius: 11
            x: paper.width - width / 2 - 4
            y: -height / 2 + 4
            color: note.board.partner
            border.color: Theme.surface
            border.width: 2
            Icon {
                anchors.centerIn: parent
                name: "pencil"
                size: 12
                color: Theme.contrast(note.board.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
            }
        }

        // How many more characters it holds, near the end of them.
        Text {
            id: count
            objectName: "desktopNoteCount"
            readonly property int remaining: note.board.notes.maxLength - note.text.length
            visible: note.editing && remaining <= 200
            x: 14
            y: paper.height - height - 10
            text: remaining > 0 ? remaining + " left" : "Full"
            color: note.ink
            opacity: 0.6
            font.family: note.board.noteFont
            font.pixelSize: Theme.fontSizeCaption
            property real shake: 0
            transform: Translate { x: count.shake }
            SequentialAnimation {
                id: full
                NumberAnimation { target: count; property: "shake"; to: -4; duration: Theme.duration(45) }
                NumberAnimation { target: count; property: "shake"; to: 4; duration: Theme.duration(70) }
                NumberAnimation { target: count; property: "shake"; to: 0; duration: Theme.duration(55) }
            }
        }

        // The doodle in its corner, which wiggles while the note is hovered.
        Item {
            id: doodle
            readonly property var art: note.doodles[note.color] || note.doodles.sakura
            width: 26
            height: 26
            x: paper.width - width - 9
            y: paper.height - height - 7
            rotation: -8
            Shape {
                width: 24
                height: 24
                scale: doodle.width / 24
                transformOrigin: Item.TopLeft
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.alpha(note.ink, 0.6)
                    strokeWidth: 1.5
                    fillColor: Qt.rgba(1, 1, 1, Theme.light ? 0.55 : 0.14)
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: doodle.art.outline }
                }
                ShapePath {
                    strokeColor: Theme.alpha(note.ink, 0.6)
                    strokeWidth: 1.3
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: doodle.art.lines || "" }
                }
                ShapePath {
                    strokeColor: "transparent"
                    fillColor: Theme.alpha(note.ink, 0.7)
                    PathSvg { path: doodle.art.dots || "" }
                }
                ShapePath {
                    strokeColor: "transparent"
                    fillColor: Qt.hsla(Theme.noteHues.sakura, 0.8, Theme.light ? 0.76 : 0.66, 0.85)
                    PathSvg { path: doodle.art.blush || "" }
                }
            }
            SequentialAnimation on rotation {
                running: note.hovered && Theme.duration(100) > 0
                loops: Animation.Infinite
                NumberAnimation { to: -16; duration: Theme.duration(260); easing.type: Easing.InOutSine }
                NumberAnimation { to: 0; duration: Theme.duration(260); easing.type: Easing.InOutSine }
                onStopped: doodle.rotation = -8
            }
        }

        // The washi tape across its top: a see-through strip of the neighbouring pastel with pale
        // stripes, its ends cut zigzag as pinking shears cut it.
        Shape {
            id: tape
            readonly property real tooth: 2.5
            width: Math.round(paper.width * 0.42)
            height: 20
            x: Math.round((paper.width - width) / 2) + [-14, 10, -4, 16, -10, 6][note.noteId % 6]
            y: -height / 2 - 1
            rotation: [3, -4, 2.5, -2, 4, -3][note.noteId % 6]
            antialiasing: true
            preferredRendererType: Shape.CurveRenderer
            // Its outline: straight along the top and foot, zigzag at either end.
            readonly property var outline: {
                const points = [Qt.point(tooth, 0), Qt.point(width - tooth, 0)]
                for (let y = 2.5, out = true; y < height; y += 2.5, out = !out)
                    points.push(Qt.point(out ? width : width - tooth, y))
                points.push(Qt.point(width - tooth, height), Qt.point(tooth, height))
                for (let y = height - 2.5, out = true; y > 0; y -= 2.5, out = !out)
                    points.push(Qt.point(out ? 0 : tooth, y))
                points.push(Qt.point(tooth, 0))
                return points
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.noteTape(note.color)
                PathPolyline { path: tape.outline }
            }
            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0; y1: 0; x2: 4; y2: 4
                    spread: ShapeGradient.RepeatSpread
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, Theme.light ? 0.38 : 0.16) }
                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, Theme.light ? 0.38 : 0.16) }
                    GradientStop { position: 0.5; color: Qt.rgba(1, 1, 1, 0) }
                    GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
                }
                PathPolyline { path: tape.outline }
            }
        }
    }

    // The doodles on a 24-unit grid: an outline filled pale, lines, dots in the ink and blush.
    readonly property var doodles: ({
        // A cat's face, its ears up, whiskers and blushing cheeks.
        sakura: {
            outline: "M5.5 10 5 4l4.5 3c1.6-.6 3.4-.6 5 0l4.5-3-.5 6c.9 1.1 1.5 2.5 1.5 4 0 3.4-3.4 5.5-8 5.5s-8-2.1-8-5.5c0-1.5.6-2.9 1.5-4z",
            lines: "M10.3 15.3c.4.7 1.3.7 1.7 0 .4.7 1.3.7 1.7 0 M1.6 13.2l2.8.6 M2 16.5l2.6-.9 M22.4 13.2l-2.8.6 M22 16.5l-2.6-.9",
            dots: "M8 12.6a1 1 0 1 0 2 0a1 1 0 1 0-2 0z M14 12.6a1 1 0 1 0 2 0a1 1 0 1 0-2 0z",
            blush: "M6.2 15.4a1.3 .8 0 1 0 2.6 0a1.3 .8 0 1 0-2.6 0z M15.2 15.4a1.3 .8 0 1 0 2.6 0a1.3 .8 0 1 0-2.6 0z"
        },
        // A small cloud with a sleepy smile.
        sky: {
            outline: "M17.5 19H9a7 7 0 1 1 6.71-9h1.79a4.5 4.5 0 1 1 0 9z",
            lines: "M10.6 15.6c.6.6 1.4.6 2 0",
            dots: "M8.7 13.4a.85 .85 0 1 0 1.7 0a.85 .85 0 1 0-1.7 0z M12.8 13.4a.85 .85 0 1 0 1.7 0a.85 .85 0 1 0-1.7 0z",
            blush: "M7 15.2a1 .6 0 1 0 2 0a1 .6 0 1 0-2 0z M14.6 15.2a1 .6 0 1 0 2 0a1 .6 0 1 0-2 0z"
        },
        // A sprout of two leaves.
        mint: {
            outline: "M12 13C12 9 9 6.5 5 6.5 5 10.5 8 13 12 13z M12 11c.3-3.5 3-6 7-6 0 4-3 6.3-7 6z",
            lines: "M12 20.5V11 M8.5 20.5h7"
        },
        // A star with a little face.
        lemon: {
            outline: "M11.525 2.295a.53.53 0 0 1 .95 0l2.31 4.679a2.123 2.123 0 0 0 1.595 1.16l5.166.756a.53.53 0 0 1 .294.904l-3.736 3.638a2.123 2.123 0 0 0-.611 1.878l.882 5.14a.53.53 0 0 1-.771.56l-4.618-2.428a2.122 2.122 0 0 0-1.973 0L6.396 21.01a.53.53 0 0 1-.77-.56l.881-5.139a2.122 2.122 0 0 0-.611-1.879L2.16 9.795a.53.53 0 0 1 .294-.906l5.165-.755a2.122 2.122 0 0 0 1.597-1.16z",
            lines: "M11.2 14.3c.5.5 1.1.5 1.6 0",
            dots: "M9.6 12.2a.8 .8 0 1 0 1.6 0a.8 .8 0 1 0-1.6 0z M12.8 12.2a.8 .8 0 1 0 1.6 0a.8 .8 0 1 0-1.6 0z"
        },
        // A heart and a twinkle beside it.
        lavender: {
            outline: "M19 14c1.49-1.46 3-3.21 3-5.5A5.5 5.5 0 0 0 16.5 3c-1.76 0-3 .5-4.5 2-1.5-1.5-2.74-2-4.5-2A5.5 5.5 0 0 0 2 8.5c0 2.3 1.5 4.05 3 5.5l7 7z",
            lines: "M21 17.5v4 M19 19.5h4"
        }
    })
}
