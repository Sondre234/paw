// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A switch drawn as a track with a knob that slides across, the track in the accent while it is
// on: dimmed while `dimmed` (as it is disabled), and with `focusRing` outlined while the keyboard
// is at it. It shows no text unless given a contentItem.
Switch {
    id: toggle
    property bool focusRing: false
    property bool dimmed: !enabled
    indicator: Rectangle {
        x: toggle.leftPadding; y: parent.height / 2 - height / 2
        width: 2 * height; height: Theme.iconSize; radius: height / 2
        opacity: toggle.dimmed ? 0.5 : 1
        color: toggle.checked ? Theme.accent : Theme.macos ? Theme.switchTrack : Theme.selected
        border.width: toggle.focusRing ? 1 : 0
        border.color: toggle.visualFocus ? Theme.focusRing : "transparent"
        Rectangle {
            x: toggle.checked ? parent.width - width - Theme.spacingXS : Theme.spacingXS
            y: Theme.spacingXS; width: parent.height - 2 * Theme.spacingXS; height: width; radius: width / 2
            color: Theme.macos ? Theme.knob : toggle.checked ? Theme.textOnAccent : Theme.text
            border.color: Theme.macos ? Theme.knobOutline : "transparent"
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
    }
    contentItem: Item {}
}
