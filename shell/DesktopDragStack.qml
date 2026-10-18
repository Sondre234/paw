// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The picture a drag of the desktop's icons carries out of the desktop (DesktopIconGrid), which
// the compositor draws under the pointer over any window: the dragged icons fanned as their stack
// is on the desktop, each on a little card tinted in the accent or its partner by turns so that the
// fan shows whatever the icons are, the one under the pointer on top with its name on the accent
// and, with others, their count in the second hue. It never shows itself; the grid takes a picture
// of it.
Item {
    id: stack
    // The grid it draws for, and the dragged icons (DesktopIcon), the one under the pointer first.
    required property Item board
    property var tiles: []
    // Room around it, and the top left of the cell of the icon under the pointer, where the rest
    // fan out from, up and to the right.
    readonly property int pad: 6
    readonly property point step: Qt.point(11, 6)
    readonly property point origin: Qt.point(pad, pad + 3 * step.y)
    // Whether every picture in it has loaded.
    readonly property bool ready: {
        for (let i = 0; i < fanned.count; ++i)
            if (!fanned.itemAt(i) || !fanned.itemAt(i).ready)
                return false
        return true
    }
    visible: false
    width: board.cellWidth + 3 * step.x + 2 * pad
    height: board.cellHeight + 3 * step.y + 2 * pad

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
            x: stack.origin.x + index * stack.step.x
            y: stack.origin.y - index * stack.step.y
            width: stack.board.cellWidth
            height: stack.board.cellHeight
            rotation: [0, -9, 8, -5][index]
            antialiasing: true

            Rectangle {
                readonly property color tint: card.index % 2 ? stack.board.partner : Theme.accent
                x: art.x - Theme.spacingM
                y: art.y - Theme.spacingM
                width: art.width + 2 * Theme.spacingM
                height: art.height + 2 * Theme.spacingM
                radius: Theme.radiusLarge
                antialiasing: true
                color: Theme.mix(Theme.surface, tint, 0.2)
                border.width: 1.5
                border.color: Theme.alpha(tint, 0.75)
            }
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
