// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A round button with a cross that closes, dismisses or clears something: a notification card or
// one in the history, a window in a stack's list, the start menu's search, a message across the
// bar. Muted at rest, the cross darkens under the pointer, on the hover state's fill or, for
// closing a window (`danger`), on the danger colour.
FlatButton {
    id: button
    property bool danger: false
    property real size: Theme.iconSize + Theme.spacingS
    implicitWidth: size; implicitHeight: size
    radius: size / 2
    padding: 0
    focusPolicy: Qt.NoFocus
    background: ButtonFill {
        radius: button.radius
        hovered: button.hovered
        pressed: button.pressed
        color: button.danger && button.hovered ? Theme.dangerFill : stateColor
    }
    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: "x"; size: Theme.iconSizeSmall
            color: button.danger && button.hovered ? Theme.textOnDanger
                 : button.hovered ? Theme.text : Theme.textMuted
        }
    }
}
