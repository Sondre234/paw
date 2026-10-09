// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// What a list says while it has nothing to show, centred across its width: a quiet line icon
// (`icon`), a line saying so (`title`) and, when there is one, a hint of what to try (`hint`). The
// notifications, the command palette and the start menu's search say it so.
Column {
    property string icon
    property string title
    property string hint
    topPadding: Theme.spacingL; bottomPadding: Theme.spacingL
    spacing: Theme.spacingS
    Icon {
        anchors.horizontalCenter: parent.horizontalCenter
        name: parent.icon; size: Theme.appIconSizeLarge; color: Theme.textDisabled
    }
    Text {
        width: parent.width
        text: parent.title; textFormat: Text.PlainText
        color: Theme.text
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.Medium
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
    }
    Text {
        visible: text !== ""
        width: parent.width
        text: parent.hint; textFormat: Text.PlainText
        color: Theme.textMuted
        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
        horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
    }
}
