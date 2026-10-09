// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// An application pinned to the start menu: its icon over its name. `app` is its record, and
// `current` says the keyboard is at it.
AbstractButton {
    id: tile
    required property var app
    property bool current: false
    // A right press, for the application's menu, where it was.
    signal menuRequested(real x, real y)
    objectName: "startTile:" + app.appId
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    Accessible.name: app.name
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onPressed: (mouse) => tile.menuRequested(mouse.x, mouse.y)
    }
    background: Rectangle {
        radius: Theme.radiusMedium
        color: tile.pressed ? Theme.pressed : tile.current ? Theme.selected
             : tile.hovered ? Theme.hover : "transparent"
    }
    contentItem: Item {
        Image {
            id: icon
            anchors.horizontalCenter: parent.horizontalCenter
            y: Theme.spacingL
            width: Theme.appIconSizeLarge; height: Theme.appIconSizeLarge
            sourceSize: Qt.size(width, height)
            source: "image://icons/" + tile.app.icon
        }
        Text {
            anchors.top: icon.bottom; anchors.topMargin: Theme.spacingM
            x: Theme.spacingS; width: parent.width - 2 * Theme.spacingS
            horizontalAlignment: Text.AlignHCenter
            text: tile.app.name; textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
    }
}
