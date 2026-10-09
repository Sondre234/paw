// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A battery drawn as an outline filled to its charge: red when nearly empty and not charging, in
// the accent colour while charging. `status` is the panel's statusSource. The fill and the colour
// ease to each new reading.
Item {
    id: icon
    required property var status
    // The fill's objectName, for tests.
    property string levelName: ""
    readonly property bool low: status.batteryPercent <= 15 && status.batteryState !== "charging"
    readonly property color tint: low ? Theme.danger : (status.batteryState === "charging" ? Theme.accent : Theme.text)
    property color shownTint: tint
    Behavior on shownTint { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    implicitWidth: 24; implicitHeight: 12
    Rectangle {
        width: 21; height: 12; radius: 2; color: "transparent"
        border.color: icon.shownTint; border.width: 1
        Rectangle {
            objectName: icon.levelName
            x: 2; y: 2; height: parent.height - 4; radius: 1
            width: Math.max(1, (parent.width - 4) * icon.status.batteryPercent / 100)
            Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            color: icon.shownTint
        }
    }
    Rectangle { x: 21; y: 4; width: 2; height: 4; color: icon.shownTint }
}
