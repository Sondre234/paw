// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The bell in the panel (shell.widgets.notifications = "bar"; the clock does the same anyway):
// opens the clock flyout with the notifications, shows how many have not been seen, and turns into
// a crossed-out bell while do-not-disturb is on. Right-click toggles do-not-disturb.
FlatButton {
    id: bell
    required property var panel
    objectName: "notificationBell"
    readonly property var center: shell.notifications
    readonly property string description: (center.dnd ? "Do not disturb, " : "") +
        (center.unread > 0 ? center.unread + " unread notifications" : "No new notifications")
    visible: shell.widgets.notifications === "bar" && center.serving
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    onClicked: panel.toggleAudioPopup("clock", bell)
    Accessible.name: description
    BarTip { panel: bell.panel; owner: bell; text: bell.description + "\nRight-click: do not disturb" }
    active: panel.audioPopup === "clock"
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: bell.center.toggleDnd()
    }
    contentItem: Item {
        FadingIcon {
            id: glyph
            objectName: "notificationGlyph"
            anchors.centerIn: parent
            name: bell.center.dnd ? "bell-off" : "bell"
            color: bell.center.dnd ? Theme.textMuted : Theme.text
        }
        // The unread count on the bell's shoulder, as the clock's.
        Badge {
            objectName: "notificationBadge"
            // Inside the button, however short the bar (the macOS style's menu bar).
            x: parent.width / 2 + Theme.spacingXS; y: Math.max(-bell.topPadding, parent.height / 2 - Theme.iconSize + Theme.spacingXS)
            count: bell.center.unread
            muted: bell.center.dnd
        }
    }
}
