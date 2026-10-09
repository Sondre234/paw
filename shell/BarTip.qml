// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A tooltip for something on the bar, in a popup window of its own: the panel's surface is only
// as tall as the bar. Hidden while a popup is open. It fades in once the pointer has rested on
// its owner and out as the pointer leaves. Its text is shown as it is, never read as markup: it
// is often a window's title or an application's.
ToolTip {
    id: barTip
    required property Item owner
    required property var panel
    visible: owner.hovered && !owner.pressed && text.length > 0 && !panel.menuOpen
    delay: 500
    width: Math.min(implicitWidth, 420)
    // Below what is on a bar along the top: the taskbar there, or the menu bar, which is in a
    // surface of its own.
    readonly property bool below: panel.onTop || (panel.menuBarWindow !== null && owner.Window.window === panel.menuBarWindow)
    y: below ? owner.height + Theme.spacingM : -implicitHeight - Theme.spacingM
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast; easing.type: Theme.easingExit }
    }
    contentItem: Text {
        text: barTip.text; textFormat: Text.PlainText; wrapMode: Text.Wrap
        color: Theme.text; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
    }
    background: Rectangle { color: Theme.surface; border.color: Theme.border; radius: Theme.radiusSmall }
    popupType: Popup.Window
}
