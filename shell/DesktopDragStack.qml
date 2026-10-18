// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The picture a drag of the desktop's icons carries out of the desktop (DesktopIconGrid), which
// the compositor draws under the pointer over any window: the dragged icons fanned as their stack
// is on the desktop, the one under the pointer on top with its name on the accent and, with
// others, their count in the second hue. It never shows itself; the grid takes a picture of it.
Item {
    id: stack
    // The grid it draws for, and the dragged icons (DesktopIcon), the one under the pointer first.
    required property Item board
    property var tiles: []
    // Room around it, and the top left of the cell of the icon under the pointer, where the rest
    // fan out from, up and to the right.
    readonly property int pad: 6
    readonly property point origin: Qt.point(pad, pad + 3 * 5)
    // Whether every picture in it has loaded.
    readonly property bool ready: {
        for (let i = 0; i < fanned.count; ++i)
            if (!fanned.itemAt(i) || !fanned.itemAt(i).ready)
                return false
        return true
    }
    visible: false
    width: board.cellWidth + 3 * 7 + 2 * pad
    height: board.cellHeight + 3 * 5 + 2 * pad

    Repeater {
        id: fanned
        model: stack.tiles.slice(0, 4)
        delegate: Item {
            id: card
            required property var modelData
            required property int index
            readonly property bool ready: art.ready
            // Each further back a little further off, turned its own way, as on the desktop.
            z: -index
            x: stack.origin.x + index * 7
            y: stack.origin.y - index * 5
            width: stack.board.cellWidth
            height: stack.board.cellHeight
            rotation: [0, -7, 6, -4][index]
            antialiasing: true

            DesktopIconArt {
                id: art
                width: stack.board.iconSize
                height: stack.board.iconSize
                x: Math.round((card.width - width) / 2)
                y: Theme.spacingM
                icon: card.modelData.icon
                picture: card.modelData.picture
                name: card.modelData.name
            }
            // The name of the one under the pointer, on the accent as a selected icon's is.
            Rectangle {
                visible: card.index === 0
                width: Math.min(card.width - 2 * Theme.spacingXS, Math.ceil(metrics.advanceWidth) + 2 * Theme.spacingM + 2)
                height: label.height + 2 * Theme.spacingXS
                x: Math.round((card.width - width) / 2)
                y: art.y + art.height + Theme.spacingS
                radius: Math.min(height / 2, Theme.radiusMedium)
                color: Theme.accent
                TextMetrics { id: metrics; font: label.font; text: card.modelData.name }
                Text {
                    id: label
                    x: Theme.spacingM
                    y: Theme.spacingXS
                    width: parent.width - 2 * Theme.spacingM
                    text: card.modelData.name
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: Theme.textOnAccent
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
            // How many go, on the icon under the pointer.
            Rectangle {
                visible: card.index === 0 && stack.tiles.length > 1
                x: art.x + art.width - width / 2
                y: art.y - height / 3
                height: 20
                width: Math.max(height, count.implicitWidth + 2 * Theme.spacingM)
                radius: height / 2
                color: stack.board.partner
                border.color: Theme.surface
                border.width: 2
                Text {
                    id: count
                    anchors.centerIn: parent
                    text: stack.tiles.length
                    color: Theme.contrast(stack.board.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeCaption
                    font.bold: true
                }
            }
        }
    }
}
