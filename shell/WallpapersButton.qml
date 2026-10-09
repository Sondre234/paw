// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The wallpaper picker's button (shell.widgets.wallpapers = "bar").
FlatButton {
    id: wallpapersButton
    required property var panel
    objectName: "wallpapersButton"
    visible: shell.widgets.wallpapers === "bar"
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    active: panel.audioPopup === "wallpapers"
    onClicked: panel.toggleAudioPopup("wallpapers", wallpapersButton)
    Accessible.name: "Wallpapers"
    BarTip { panel: wallpapersButton.panel; owner: wallpapersButton; text: "Wallpapers" }
    contentItem: Item {
        Icon { anchors.centerIn: parent; name: "image"; color: wallpapersButton.active ? Theme.accent : Theme.text }
    }
}
