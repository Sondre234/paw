// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// One sticky note on the desktop (DesktopIconGrid, the model shell.desktopNotes): pastel paper over
// a block of the board's cells, set a little askew by its number as the icons' prints are, held on
// by a strip of washi tape across its top, with a soft shadow and, in its corner, a doodle that
// goes with its colour: a cat's face on sakura, a cloud on sky, a sprout on mint, a star on lemon,
// a heart on lavender. Its text is in a rounded hand (the board's noteFont); a line that starts
// with "[ ]" or "- [ ]" is a checklist's, its box ticked or cleared by a click, its text struck
// through once ticked; the first of several lines is its title. Hovered, it lifts a little and its
// doodle wiggles. It comes and goes in the icons' ripple, and pops in as it is made.
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
    readonly property bool hovered: hover.hovered && !board.dragging && !board.banding
    // Whether it has been in a cell: from there on it glides to new ones.
    property bool landed: false
    onPlacedChanged: if (placed) Qt.callLater(function() { note.landed = true })
    // How far it is in view: 0 hidden, 1 shown, past 1 for a moment as it pops in.
    property real reveal: 1

    width: board.cellWidth * spanColumns
    height: board.cellHeight * spanRows
    visible: placed && reveal > 0.001
    z: hovered ? 2 : 0
    x: board.cellX(column)
    y: board.cellY(row)
    Behavior on x { enabled: note.landed; NumberAnimation { duration: Theme.duration(380); easing.type: Easing.OutBack; easing.overshoot: 1.1 } }
    Behavior on y { enabled: note.landed; NumberAnimation { duration: Theme.duration(380); easing.type: Easing.OutBack; easing.overshoot: 1.1 } }

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

    // The note, inside the block with room for its tape above it and its shadow around it.
    Item {
        id: sheet
        x: Theme.spacingM
        y: Theme.spacingM + 6
        width: note.width - 2 * Theme.spacingM
        height: note.height - y - Theme.spacingM - 2
        opacity: Math.min(1, note.reveal)
        scale: (0.6 + 0.4 * note.reveal) * (note.hovered ? 1.015 : 1)
        rotation: note.hovered ? note.tilt * 0.5 : note.tilt
        Behavior on rotation { NumberAnimation { duration: Theme.durationSlow; easing.type: Easing.OutBack } }
        Behavior on scale { enabled: note.reveal === 1; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        antialiasing: true

        // Its shadow: through the GPU a soft one, deeper as it lifts; else two soft layers.
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: Shadow {
                radius: paper.radius
                blur: note.hovered ? 18 : 12
                offset: Qt.vector2d(0, note.hovered ? 8 : 5)
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
                width: parent.width
                spacing: 2
                Repeater {
                    model: note.lines
                    delegate: Item {
                        id: line
                        required property var modelData
                        required property int index
                        width: shown.width
                        height: Math.max(words.implicitHeight, modelData.box ? box.height + 4 : 0)
                        Rectangle {
                            id: box
                            objectName: "desktopNoteBox"
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
                text: "Empty note"
                color: note.ink
                opacity: 0.45
                font.family: note.board.noteFont
                font.pixelSize: note.board.noteFontSize
            }
            Rectangle {
                visible: shown.height > page.height
                anchors.bottom: parent.bottom
                width: parent.width
                height: 22
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.alpha(note.paperColor, 0) }
                    GradientStop { position: 1; color: note.paperColor }
                }
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
