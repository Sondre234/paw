// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// The user's picture in a circle, or the first letter of their name on the accent colour when
// they have none. The picture's corners are covered in `backdrop`, the colour it sits on, rather
// than masked: every renderer draws that, the software one too.
Item {
    id: avatar
    property real size: Theme.appIconSizeLarge
    property color backdrop: Theme.surface
    width: size; height: size
    readonly property bool hasPicture: picture.status === Image.Ready

    Rectangle {
        anchors.fill: parent
        visible: !avatar.hasPicture
        radius: width / 2
        color: Theme.accent
        Text {
            anchors.centerIn: parent
            text: shell.startMenu.userName.charAt(0).toUpperCase()
            color: Theme.textOnAccent
            font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
    }
    Image {
        id: picture
        objectName: "userPicture"
        anchors.fill: parent
        visible: avatar.hasPicture
        source: shell.startMenu.userIcon
        sourceSize: Qt.size(avatar.size, avatar.size)
        fillMode: Image.PreserveAspectCrop
    }
    Shape {
        anchors.fill: parent
        visible: avatar.hasPicture
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            readonly property real s: avatar.size
            readonly property real r: avatar.size / 2
            fillColor: avatar.backdrop
            strokeColor: "transparent"
            fillRule: ShapePath.OddEvenFill
            // The square, and the circle inside it left uncovered.
            PathSvg {
                path: "M0 0H" + parent.s + "V" + parent.s + "H0Z" +
                      " M0 " + parent.r + "A" + parent.r + " " + parent.r + " 0 1 0 " + parent.s + " " + parent.r +
                      "A" + parent.r + " " + parent.r + " 0 1 0 0 " + parent.r + "Z"
            }
        }
    }
}
