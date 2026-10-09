// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The appearance profile in use (shell.widgets.profiles = "bar"); clicking lists the profiles to
// switch to.
FlatButton {
    id: profilesButton
    required property var panel
    objectName: "profilesButton"
    visible: shell.widgets.profiles === "bar" && shell.profiles.length > 1
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    active: panel.audioPopup === "profiles"
    onClicked: panel.toggleAudioPopup("profiles", profilesButton)
    Accessible.name: "Appearance: " + (shell.profile || "none")
    BarTip { panel: profilesButton.panel; owner: profilesButton; text: "Appearance: " + (shell.profile || "none") }
    // Three swatches of the profile in use: accent, desktop background, text.
    contentItem: Item {
        Row {
            anchors.centerIn: parent; spacing: Theme.spacingXS
            Repeater {
                model: [shell.accent, shell.background, shell.textColor]
                // A ring in the text colour keeps a swatch close to the panel's own
                // colour visible.
                Rectangle {
                    required property color modelData
                    width: 10; height: 10; radius: 5
                    color: modelData
                    border.width: 1
                    border.color: Theme.alpha(Theme.text, 0.5)
                }
            }
        }
    }
}
