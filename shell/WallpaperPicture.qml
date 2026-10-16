// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The wallpaper's picture laid out as shell.wallpaper_mode says (`mode`): cropped to fill the item,
// fitted into it, stretched over it, or at its own size, in the middle or tiled from the top left;
// over the background colour where "fit" and "center" leave room. At its own size a pixel of the
// picture is `pixelScale` of the item's: on the desktop a pixel of the screen, on a workspace's card
// as much smaller as the card draws the output.
Item {
    id: wallpaper
    property alias source: picture.source
    property string mode: shell.wallpaperMode
    property real pixelScale: 1 / Screen.devicePixelRatio
    // The size the picture is decoded at where it is scaled to the item; unset, its own.
    property size sourceSize: Qt.size(-1, -1)
    property alias asynchronous: picture.asynchronous
    property alias cache: picture.cache
    readonly property alias status: picture.status
    readonly property bool ownSize: mode === "center" || mode === "tile"
    clip: ownSize
    Rectangle {
        anchors.fill: parent
        visible: picture.status === Image.Ready && (wallpaper.mode === "fit" || wallpaper.mode === "center")
        color: shell.background
    }
    // At its own size the picture is laid out in the screen's pixels, then scaled to the item's.
    Item {
        width: wallpaper.ownSize ? wallpaper.width / wallpaper.pixelScale : wallpaper.width
        height: wallpaper.ownSize ? wallpaper.height / wallpaper.pixelScale : wallpaper.height
        scale: wallpaper.ownSize ? wallpaper.pixelScale : 1
        transformOrigin: Item.TopLeft
        Image {
            id: picture
            objectName: "wallpaperImage"
            anchors.fill: parent
            fillMode: wallpaper.mode === "fit" ? Image.PreserveAspectFit
                    : wallpaper.mode === "stretch" ? Image.Stretch
                    : wallpaper.mode === "center" ? Image.Pad
                    : wallpaper.mode === "tile" ? Image.Tile
                    : Image.PreserveAspectCrop
            horizontalAlignment: wallpaper.mode === "tile" ? Image.AlignLeft : Image.AlignHCenter
            verticalAlignment: wallpaper.mode === "tile" ? Image.AlignTop : Image.AlignVCenter
            sourceSize: wallpaper.ownSize ? Qt.size(-1, -1) : wallpaper.sourceSize
            smooth: true
        }
    }
}
