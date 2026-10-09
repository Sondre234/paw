// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The active keyboard layout's short name ("us", "no"), shown while the keymap has more than
// one. Clicking it switches every keyboard to the next.
FlatButton {
    id: indicator
    required property var panel
    objectName: "keyboardLayout"
    readonly property var layout: shell.keyboardLayout
    readonly property string label: layout.short || ""
    readonly property string description: "Keyboard layout: " + (layout.name || label)
    visible: shell.widgets.keyboard_layout && (layout.count || 0) > 1
    Layout.preferredWidth: Math.max(Theme.barButtonWidth, text.implicitWidth + 2 * Theme.spacingM)
    Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    onClicked: { panel.closeMenus(); shell.send("switch_layout next") }
    Accessible.name: description
    BarTip { panel: indicator.panel; owner: indicator; text: indicator.description + "\nClick: next layout" }
    contentItem: Text {
        id: text
        objectName: "keyboardLayoutText"
        text: indicator.label
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
    }
}
