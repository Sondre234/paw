// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw

// A rectangle filled with `fill`, a popup's colour, that is a card of its own: on the frosted
// glass (shell.blur) the wallpaper behind it shows blurred under it, its colour as translucent as
// a popup's card (Theme.glassOpacity); without the glass, or with `glass` cleared (for what lies on
// a card already), it is filled as it is. For a card drawn by hand rather than by CardFrame: a
// tooltip, the macOS style's Notification Center.
Rectangle {
    id: pane
    property color fill: Theme.popupSurface
    property bool glass: true
    // Whether the glass shows under it, and its colour is translucent.
    readonly property bool glassy: glassPane.visible && glassPane.active
    color: glassy ? Theme.alpha(fill, Theme.glassOpacity) : fill
    Glass {
        id: glassPane
        anchors.fill: parent
        z: -1
        visible: Theme.glass && pane.glass
        radius: pane.radius
    }
}
