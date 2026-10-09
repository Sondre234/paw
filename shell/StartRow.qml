// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A row of the start menu's lists: an icon (a theme icon's name or an image's path), a title, a
// muted line under it when there is one, and muted text at its end. `current` says the keyboard
// is at it.
AbstractButton {
    id: row
    property string iconName
    property string title
    property string subtitle
    property string trailing
    property real iconSize: Theme.appIconSizeLarge
    property bool current: false
    // A right press, for the application's menu, where it was.
    signal menuRequested(real x, real y)
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: (mouse) => row.menuRequested(mouse.x, mouse.y)
    }
    implicitHeight: Math.max(iconSize, titleText.implicitHeight + (subtitle ? subtitleText.implicitHeight : 0)) +
                    2 * Theme.spacingM
    Accessible.name: title
    background: Rectangle {
        radius: Theme.radiusMedium
        color: row.pressed ? Theme.pressed : row.current ? Theme.selected
             : row.hovered ? Theme.hover : "transparent"
    }
    contentItem: Item {
        Image {
            id: image
            x: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            width: row.iconSize; height: row.iconSize
            sourceSize: Qt.size(width, height)
            source: row.iconName === "" ? "" : "image://icons/" + row.iconName
        }
        Column {
            anchors.left: image.right; anchors.leftMargin: Theme.spacingL
            anchors.right: trailingText.left; anchors.rightMargin: trailingText.text ? Theme.spacingL : 0
            anchors.verticalCenter: parent.verticalCenter
            Text {
                id: titleText
                width: parent.width
                text: row.title; textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.text
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
            }
            Text {
                id: subtitleText
                width: parent.width
                visible: row.subtitle !== ""
                text: row.subtitle; textFormat: Text.PlainText
                elide: Text.ElideRight
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
        Text {
            id: trailingText
            anchors.right: parent.right; anchors.rightMargin: text ? Theme.spacingL : 0
            anchors.verticalCenter: parent.verticalCenter
            text: row.trailing; textFormat: Text.PlainText
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
