// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A count on a pill, as the clock's unread notifications and a stacked button's windows: in the
// accent colour, or muted while do-not-disturb holds the cards back; above 9 it says 9+. It is
// there while `count` is above zero: it grows in and shrinks away, the room it takes in a row
// opening and closing with it, and a count going up gives it a short pop.
Item {
    id: badge
    property int count: 0
    property bool muted: false
    // The count it shows, kept while it shrinks away from zero.
    property int shownCount: 0
    Component.onCompleted: shownCount = count
    onCountChanged: {
        if (count > 0) {
            if (count > shownCount && grow === 1)
                popping.restart()
            shownCount = count
        }
    }
    // How far it is in, from 0 to 1.
    property real grow: count > 0 ? 1 : 0
    Behavior on grow {
        NumberAnimation {
            duration: badge.count > 0 ? Theme.durationNormal : Theme.durationFast
            easing.type: badge.count > 0 ? Theme.easing : Theme.easingExit
        }
    }
    property real pop: 1
    SequentialAnimation {
        id: popping
        NumberAnimation { target: badge; property: "pop"; to: 1.2; duration: Theme.durationFast / 2; easing.type: Theme.easing }
        NumberAnimation { target: badge; property: "pop"; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    visible: grow > 0
    implicitWidth: pill.width * grow
    implicitHeight: pill.height
    Rectangle {
        id: pill
        anchors.centerIn: parent
        height: label.implicitHeight + Theme.spacingXS
        width: Math.max(height, label.implicitWidth + 2 * Theme.spacingS)
        radius: height / 2
        opacity: badge.grow
        scale: (Theme.growFrom + (1 - Theme.growFrom) * badge.grow) * badge.pop
        color: badge.muted ? Theme.selected : Theme.accent
        Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Text {
            id: label
            anchors.centerIn: parent
            text: badge.shownCount > 9 ? "9+" : badge.shownCount
            color: badge.muted ? Theme.textMuted : Theme.textOnAccent
            font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
    }
}
