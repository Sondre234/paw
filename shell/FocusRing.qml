// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The ring that says the keyboard is at something, as a framed button's (PushButton) says it: an
// outline in Theme.focusRing around its parent, `spread` outside its edge, or on the edge with
// none where what lies outside would be cut off (the task list clips its buttons, the card its
// pictures). Its corners are its user's to match: the parent's radius and the spread.
Rectangle {
    property real spread: 0
    anchors.fill: parent
    anchors.margins: -spread
    color: "transparent"
    border.color: Theme.focusRing
    border.width: Theme.focusRingWidth
}
