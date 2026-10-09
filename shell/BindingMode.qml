// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The binding mode in use (`modes` in the configuration), its name on a pill in the accent
// colour while it is not the default one, as sway's bar shows it. A click leaves it.
FlatButton {
    id: chip
    required property var panel
    objectName: "bindingMode"
    readonly property string mode: shell.bindingMode
    visible: mode !== ""
    Layout.preferredWidth: pill.width + 2 * Theme.spacingXS
    Layout.preferredHeight: Theme.barButtonHeight
    onClicked: { panel.closeMenus(); shell.send("mode default") }
    Accessible.name: "Binding mode: " + mode
    BarTip {
        panel: chip.panel; owner: chip
        text: "Binding mode: " + chip.mode + "\nClick: leave it"
    }
    contentItem: Item {
        Rectangle {
            id: pill
            anchors.centerIn: parent
            width: label.implicitWidth + 2 * Theme.spacingS
            height: label.implicitHeight + Theme.spacingXS
            radius: height / 2
            color: Theme.accent
            Text {
                id: label
                objectName: "bindingModeText"
                anchors.centerIn: parent
                text: chip.mode
                color: Theme.textOnAccent
                font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold
                font.family: Theme.fontFamily
            }
        }
    }
}
