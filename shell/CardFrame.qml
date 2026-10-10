// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// A card's frame, filling its parent beneath what the card holds: its shadow, drawn through the
// GPU (Theme.effects) while `shadow`, then its surface in `color` with `radius` corners and its
// `outline`, and with `innerEdge` the macOS style's light line inside that (Theme.popupInnerEdge).
Item {
    id: frame
    property real radius: Theme.radiusLarge
    property color color: Theme.popupSurface
    property color outline: Theme.popupOutline
    property bool innerEdge: false
    property bool shadow: true
    anchors.fill: parent
    Loader {
        anchors.fill: parent
        active: Theme.effects && frame.shadow
        sourceComponent: RectangularShadow {
            radius: frame.radius
            blur: Theme.shadowBlur
            offset: Qt.vector2d(0, Theme.shadowOffset)
            color: Theme.shadow
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: frame.radius
        color: frame.color
        border.color: frame.outline
        Rectangle {
            visible: frame.innerEdge && Theme.popupInnerEdge.a > 0 && frame.color.a > 0
            anchors.fill: parent; anchors.margins: 1
            radius: frame.radius - 1
            color: "transparent"
            border.color: Theme.popupInnerEdge
        }
    }
}
