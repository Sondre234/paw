// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The battery: an outline filled to the charge (BatteryIcon.qml), and the charge.
FlatButton {
    id: battery
    required property var panel
    objectName: "batteryWidget"
    readonly property var status: battery.panel.statusSource
    readonly property bool low: icon.low
    readonly property color tint: icon.tint
    visible: shell.widgets.battery === "bar" && status.batteryPresent
    Layout.preferredWidth: charge.implicitWidth + 2 * Theme.spacingM; Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    Accessible.name: status.batteryText
    BarTip { panel: battery.panel; owner: battery; text: battery.status.batteryText }
    contentItem: Row {
        id: charge
        spacing: Theme.spacingS
        anchors.centerIn: parent
        BatteryIcon {
            id: icon
            anchors.verticalCenter: parent.verticalCenter
            status: battery.status
            levelName: "batteryLevel"
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: battery.status.batteryPercent + "%"
            color: battery.tint
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
