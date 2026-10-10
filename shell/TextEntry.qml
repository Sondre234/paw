// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A field to type into, as a list or a dialog has one (a password, a PIN, a scale): the field's
// fill with `radius` corners, outlined in the accent while it has the keyboard, the text in the
// theme's colours and type.
TextField {
    id: entry
    property real radius: Theme.radiusSmall
    implicitHeight: Theme.fieldHeight
    leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
    background: Rectangle {
        radius: entry.radius
        color: Theme.fieldFill
        border.color: entry.activeFocus ? Theme.accent : Theme.border
    }
}
