// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects
import Paw

// A card's frame, filling its parent beneath what the card holds: its shadow, drawn through the
// GPU (Theme.effects) while `shadow`, then its surface in `color` with `radius` corners and its
// `outline`, and with `innerEdge` the macOS style's light line inside that (Theme.popupInnerEdge).
// With the frosted glass (shell.blur) the surface is laid translucent (Theme.onGlass) over the
// wallpaper behind it, blurred, unless `glass` is cleared.
Item {
    id: frame
    property real radius: Theme.radiusLarge
    property color color: Theme.popupSurface
    property color outline: Theme.popupOutline
    property int outlineWidth: Theme.outlineWidth
    property bool innerEdge: false
    property bool shadow: true
    property bool glass: true
    // Whether the glass shows under it, and the surface is translucent.
    readonly property bool glassy: glassPane.visible && glassPane.active
    anchors.fill: parent
    Loader {
        anchors.fill: parent
        active: Theme.effects && frame.shadow
        sourceComponent: RectangularShadow {
            radius: frame.radius
            blur: Theme.shadowBlur
            offset: Qt.vector2d(Theme.shadowOffsetX, Theme.shadowOffset)
            color: Theme.shadow
        }
    }
    Glass {
        id: glassPane
        objectName: "cardGlass"
        anchors.fill: parent
        visible: frame.glass && Theme.glass
        radius: frame.radius
    }
    Rectangle {
        objectName: "cardSurface"
        anchors.fill: parent
        radius: frame.radius
        color: frame.glassy ? Theme.onGlass(frame.color) : frame.color
        border.color: frame.outline
        border.width: frame.outlineWidth
        // On the glass, a sheen down from its top edge, as light falling on a pane.
        Rectangle {
            visible: frame.glassy
            anchors.fill: parent; anchors.margins: frame.outlineWidth
            radius: frame.radius - frame.outlineWidth
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.glassSheen }
                GradientStop { position: 0.5; color: Theme.alpha(Theme.glassSheen, 0) }
            }
        }
        // The macOS style's light line inside the outline, and the glass's, as light catching a
        // pane's edge.
        Rectangle {
            readonly property color edge: frame.glassy ? Theme.glassEdge : Theme.popupInnerEdge
            visible: (frame.innerEdge || frame.glassy) && edge.a > 0 && frame.color.a > 0
            anchors.fill: parent; anchors.margins: frame.outlineWidth
            radius: frame.radius - frame.outlineWidth
            color: "transparent"
            border.color: edge
        }
    }
}
