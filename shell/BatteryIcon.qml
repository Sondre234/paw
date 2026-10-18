// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A battery drawn as an outline filled to its charge: red when nearly empty and not charging, in
// the accent colour while charging. `status` is the panel's statusSource. The fill and the colour
// ease to each new reading. It is drawn to stand beside line icons `size` large.
Item {
    id: icon
    required property var status
    property real size: Theme.iconSize
    readonly property real unit: size / 18
    // The fill's objectName, for tests.
    property string levelName: ""
    readonly property bool low: status.batteryPercent <= 15 && status.batteryState !== "charging"
    readonly property color tint: low ? Theme.danger : (status.batteryState === "charging" ? Theme.accent : Theme.text)
    property color shownTint: tint
    Behavior on shownTint { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    implicitWidth: body.width + nub.width + 1; implicitHeight: body.height
    Rectangle {
        id: body
        readonly property int inset: Math.round(2 * icon.unit)
        width: Math.round(21 * icon.unit); height: Math.round(12 * icon.unit); radius: 2 * icon.unit
        color: "transparent"
        border.color: icon.shownTint; border.width: Math.max(1, Math.round(icon.unit))
        Rectangle {
            objectName: icon.levelName
            x: body.inset; y: body.inset; height: parent.height - 2 * body.inset; radius: icon.unit
            width: Math.max(1, (parent.width - 2 * body.inset) * icon.status.batteryPercent / 100)
            Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            color: icon.shownTint
        }
    }
    Rectangle {
        id: nub
        x: body.width; y: Math.round(4 * icon.unit)
        width: Math.round(2 * icon.unit); height: body.height - 2 * y
        color: icon.shownTint
    }
}
