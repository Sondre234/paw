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
    Layout.preferredWidth: Theme.barVertical ? Theme.barItemWidth : charge.implicitWidth + 2 * Theme.spacingM
    Layout.preferredHeight: Theme.barVertical ? charge.implicitHeight + 2 * Theme.spacingS : Theme.barButtonHeight
    hoverEnabled: true
    Accessible.name: status.batteryText
    BarTip { panel: battery.panel; owner: battery; text: battery.status.batteryText }
    // The charge beside the outline, or under it along a side.
    contentItem: Grid {
        id: charge
        spacing: Theme.spacingS
        anchors.centerIn: parent
        columns: Theme.barVertical ? 1 : 2
        horizontalItemAlignment: Grid.AlignHCenter; verticalItemAlignment: Grid.AlignVCenter
        BatteryIcon {
            id: icon
            status: battery.status
            levelName: "batteryLevel"
        }
        Text {
            text: battery.status.batteryPercent + "%"
            color: battery.tint
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
