// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The picture of a desktop icon (DesktopIcon), as large as the item: its icon, or a picture file
// as a print with a white border, wider at the foot, set askew by its name. The stack a drag of
// the icons carries out of the desktop (DesktopDragStack) draws it too.
Item {
    id: art
    // For image://icons/, a theme icon's names or a path; a picture file's URL, drawn in its
    // place; and the name, which sets how askew the print is.
    property string icon
    property string picture
    property string name
    // Whether it is drawn: neither picture is still loading.
    readonly property bool ready: (art.picture === "" ? themed : photo).status !== Image.Loading

    Image {
        id: themed
        anchors.fill: parent
        visible: art.picture === ""
        source: art.picture === "" && art.icon !== "" ? shell.iconBase + art.icon : ""
        sourceSize: Qt.size(width, height)
        smooth: true
    }
    Rectangle {
        id: snapshot
        visible: art.picture !== ""
        anchors.centerIn: parent
        width: Math.round(parent.width * 0.86)
        height: Math.round(width * 1.12)
        color: "#fbfaf6"
        radius: 2
        rotation: [-5, 3, -2, 4, -3, 2][(art.name.length + art.name.charCodeAt(0)) % 6]
        antialiasing: true
        // Its shadow on the desktop.
        Rectangle {
            z: -1
            x: 1.5; y: 2.5
            width: parent.width; height: parent.height
            radius: parent.radius
            color: Qt.rgba(0, 0, 0, 0.22)
        }
        Image {
            id: photo
            x: 3; y: 3
            width: parent.width - 6
            height: parent.height - 3 - Math.round(parent.height * 0.16)
            source: art.picture
            sourceSize: Qt.size(width * 2, height * 2)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: false
            smooth: true
        }
    }
}
