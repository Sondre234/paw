// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// An application's status icon in the tray (a StatusNotifierItem), its attention icon while
// it needs attention. Left-click activates the application (or opens the menu of an item that
// is only a menu), middle-click is its secondary action, right-click opens its menu, and the
// wheel scrolls it.
FlatButton {
    id: trayButton
    required property string key
    required property string title
    required property string status
    required property string image
    required property string toolTip
    required property bool itemIsMenu
    required property bool hasMenu
    // The panel it is on, which opens its menu.
    required property Item panel
    objectName: "trayItem"
    visible: status !== "Passive"
    width: Theme.barButtonWidth - Theme.spacingM; height: Theme.barButtonHeight
    hoverEnabled: true
    active: panel.trayMenuKey === key
    Accessible.name: title
    onClicked: panel.trayActivate(trayButton)
    BarTip { panel: trayButton.panel; owner: trayButton; text: trayButton.toolTip }
    contentItem: Item {
        Image {
            objectName: "trayIcon"
            readonly property int size: Theme.macos ? Theme.menuBarIconSize
                                        : Math.max(Theme.spacingL, Math.min(Theme.iconSize, Theme.barButtonHeight - 2 * Theme.spacingM))
            anchors.centerIn: parent
            width: size; height: size
            source: trayButton.image; sourceSize: Qt.size(size, size)
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                trayButton.panel.trayMenu(trayButton)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton)
                trayButton.panel.traySecondary(trayButton)
        }
    }
    // A notch at a time, in Qt's units (120 a notch), as KDE's tray sends it.
    WheelHandler {
        // Qt takes the whole pointer for a touchpad once the compositor offers gestures
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real travel: 0
        onWheel: (event) => {
            var horizontal = event.angleDelta.y === 0
            travel += horizontal ? event.angleDelta.x : event.angleDelta.y
            var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
            travel -= steps * 120
            if (steps !== 0)
                shell.tray.scroll(trayButton.key, steps * 120, horizontal)
        }
    }
}
