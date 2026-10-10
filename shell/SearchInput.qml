// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A search field, as the start menu's and the command palette's: a rounded field on the raised
// surface with a magnifier before the text, outlined in the accent colour while it has the
// keyboard; in the macOS style a rounded rectangle tinted with the text colour. `large` is the
// palette's, the one thing on its card, in the title's type; in the macOS style it is Spotlight's,
// with no frame of its own, the card's top.
TextField {
    id: field
    property bool large: false
    readonly property bool spotlight: large && Theme.macos
    readonly property real iconSize: spotlight ? Theme.spotlightGlyph : large ? Theme.iconSize : Theme.iconSizeSmall
    implicitHeight: spotlight ? Theme.spotlightFieldHeight : Theme.fieldHeight + (large ? Theme.spacingXS : 0)
    leftPadding: Theme.spacingL + iconSize + Theme.spacingM
    rightPadding: Theme.spacingL
    color: Theme.text
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: spotlight ? Theme.spotlightFontSize : large ? Theme.fontSizeTitle : Theme.fontSize
    font.family: Theme.fontFamily
    background: Rectangle {
        radius: Theme.macos ? Theme.radiusSmall + 1 : height / 2
        color: field.spotlight ? "transparent" : Theme.fieldFill
        border.color: field.spotlight ? "transparent" : field.activeFocus ? Theme.accent : Theme.border
        Icon {
            x: Theme.spacingL; anchors.verticalCenter: parent.verticalCenter
            name: "search"; size: field.iconSize
            color: field.activeFocus && !field.spotlight ? Theme.text : Theme.textMuted
        }
    }
}
