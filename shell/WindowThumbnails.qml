// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import Paw

// The windows of the taskbar button the pointer rests on, with shell.thumbnails: a tile for each,
// side by side in the order the stack's list has them, with the application's icon, the window's
// title and, while the pointer is on the tile, a cross that closes the window, over a small
// picture of it. The picture is the task model's (its `picture` role, while the tile wants it
// through the panel's wantPicture) and follows the window while shown with
// shell.liveThumbnails; until one has come, or without one, the application's icon stands in for
// it, and the first fades in over the icon. The focused window's tile is marked as the bar marks
// it: selected, with a line in the accent colour (the urgent one for a window asking for
// attention) under it. Clicking a tile focuses its window (or minimizes it when focused already),
// the cross or a middle click closes it, and a right click opens its menu. How wide the pictures
// are, and whether there is room for them at all rather than the list, is the panel's
// (thumbnailWidth). While a window's process plays sound, a speaker before the cross says so and
// mutes it, crossed out while muted (WindowSound).
PopupCard {
    id: thumbnails
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "windowThumbnails"
    open: panel.thumbnailsOpen
    // Shown on hover, it leaves the keyboard where it is.
    initialFocus: null
    readonly property TaskFilter windows: panel.groupWindows
    readonly property real tileGap: panel.thumbnailGap
    readonly property real pictureWidth: panel.thumbnailWidth
    readonly property real pictureHeight: Math.round(pictureWidth * 0.625)
    // A tile's header is as tall as the cross in it.
    readonly property real headerHeight: Theme.iconSize + Theme.spacingS
    readonly property real tileWidth: pictureWidth + 2 * panel.thumbnailPadding
    readonly property real tileHeight: 2 * panel.thumbnailPadding + headerHeight + Theme.spacingS + pictureHeight
    readonly property int count: Math.max(1, windows.count)
    implicitWidth: 2 * tileGap + count * tileWidth + (count - 1) * tileGap
    implicitHeight: 2 * tileGap + tileHeight
    anchorRect: panel.dockAnchor(panel.groupX, 0)
    side: panel.dockSide
    bounds: panel.popupArea
    // Open, it glides to another button's windows and eases to their width, as on Windows 11.
    glides: true
    HoverHandler {
        id: hover
        onHoveredChanged: thumbnails.panel.hoverGroupList(hovered)
    }
    readonly property bool hovered: hover.hovered
    // The tile of the window at `index`, for the keyboard on the bar (the panel's barKeys).
    function windowAt(index) { return tiles.itemAt(index) }
    // The last of its windows closing closes it.
    Connections {
        target: thumbnails.windows
        function onCountChanged() {
            if (thumbnails.windows.count === 0 && thumbnails.open)
                thumbnails.panel.groupOpen = false
        }
    }
    // Where the row of tiles below goes: they keep their size while the card eases to another,
    // centred in it and cut to it.
    Item {
        id: tileArea
        anchors.fill: parent; anchors.margins: thumbnails.tileGap
        clip: true
    }
    Row {
        parent: tileArea
        anchors.centerIn: parent
        spacing: thumbnails.tileGap
        Repeater {
            id: tiles
            model: thumbnails.visible ? thumbnails.windows : null
            delegate: Button {
                id: tile
                required property int index
                required property int taskId
                required property string title
                required property string appId
                required property bool active
                required property bool minimized
                required property bool urgent
                // The row itself, for its picture, which a stand-in model may not have.
                required property var model
                objectName: "windowThumbnail"
                width: thumbnails.tileWidth; height: thumbnails.tileHeight
                padding: thumbnails.panel.thumbnailPadding
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.name: title
                // Closing the card destroys this tile, so it goes last.
                onClicked: { thumbnails.panel.taskSource.activate(taskId); thumbnails.panel.groupOpen = false }
                // The tile wants its window's picture while it is there (the panel's
                // wantPicture), taking over from the panel's own want as the card opens: the
                // window it wanted, of the panel it asked.
                property Item owner: null
                property int watched: -1
                Component.onCompleted: {
                    owner = thumbnails.panel
                    watched = taskId
                    owner.wantPicture(watched, true)
                }
                Component.onDestruction: if (owner) owner.wantPicture(watched, false)
                readonly property bool marked: active || urgent
                // The keyboard on the bar is at it, which rings it, peeks at its window as the
                // pointer resting on its picture does, and shows its cross for Delete; the Menu
                // key there opens its window's menu.
                readonly property bool keySelected: thumbnails.panel.barKeys.active &&
                                                    thumbnails.panel.barKeys.window === index
                function keyMenu() { thumbnails.panel.openContextMenu(tile, 0, tile.taskId, tile.appId) }
                background: ButtonFill {
                    // Lit under a drag too, which brings its window forward (the panel's dragDelay).
                    hovered: tile.hovered || thumbnails.panel.dragTile === tile
                    pressed: tile.pressed
                    active: tile.active
                    // On its edge, which the card clips at.
                    FocusRing {
                        objectName: "windowThumbnailFocusRing"
                        visible: tile.keySelected
                        radius: parent.radius
                    }
                    Rectangle {
                        objectName: "windowThumbnailLine"
                        visible: tile.marked
                        anchors.bottom: parent.bottom; anchors.bottomMargin: Theme.spacingXS
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Theme.iconSizeSmall; height: 3; radius: 1
                        color: tile.urgent ? Theme.urgent : Theme.accent
                    }
                }
                contentItem: ColumnLayout {
                    spacing: Theme.spacingS
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredHeight: thumbnails.headerHeight
                        spacing: Theme.spacingS
                        Image {
                            Layout.leftMargin: Theme.spacingXS
                            Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                            source: "image://icons/" + thumbnails.panel.groupIcon
                            sourceSize: Qt.size(Theme.iconSizeSmall, Theme.iconSizeSmall)
                        }
                        Text {
                            objectName: "windowThumbnailTitle"
                            Layout.fillWidth: true
                            text: tile.title; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: tile.urgent ? Theme.urgent : tile.minimized ? Theme.textMuted : Theme.text
                            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        }
                        // The sound the window's process plays, as a browser's tab shows it: a
                        // speaker that mutes it, crossed out while muted, to unmute it. It fades
                        // in taking its room from the title's end, the title starting where it did
                        // and the cross keeping its place, and gives the room back once faded out.
                        // A click on it is its own, not the tile's.
                        MuteButton {
                            id: speaker
                            objectName: "windowThumbnailSound"
                            WindowSound {
                                id: sound
                                audio: thumbnails.panel.audioSource
                                windows: thumbnails.panel.taskSource
                                // A stand-in model may not say.
                                pid: tile.model.pid || 0
                            }
                            readonly property bool shown: sound.playing || sound.muted
                            size: closeButton.size; iconSize: Theme.iconSizeSmall
                            radius: size / 2
                            padding: 0
                            focusPolicy: Qt.NoFocus
                            // Its waves are the loudest stream's, one at the least while it plays.
                            level: Math.max(1, sound.volume)
                            muted: sound.muted
                            opacity: shown ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
                            visible: opacity > 0
                            enabled: shown
                            Accessible.name: (sound.muted ? "Unmute " : "Mute ") + tile.title
                            onClicked: sound.toggleMute()
                        }
                        // Always in its place, so that the title does not move as it shows.
                        CloseButton {
                            id: closeButton
                            objectName: "windowThumbnailClose"
                            readonly property bool shown: tile.hovered || hovered || tile.keySelected
                            opacity: shown ? 1 : 0
                            enabled: shown
                            danger: true
                            Layout.preferredWidth: size; Layout.preferredHeight: size
                            Accessible.name: "Close " + tile.title
                            onClicked: thumbnails.panel.taskSource.close(tile.taskId)
                        }
                    }
                    // The picture, as large as fits in its box, keeping its proportions; with
                    // the GPU its corners are rounded as the box's.
                    Item {
                        id: box
                        Layout.preferredWidth: thumbnails.pictureWidth; Layout.preferredHeight: thumbnails.pictureHeight
                        Rectangle {
                            objectName: "windowThumbnailStandIn"
                            anchors.fill: parent
                            // Fading out under the first picture as it fades in.
                            opacity: picture.visible ? 1 - picture.opacity : 1
                            visible: opacity > 0
                            radius: Theme.radiusSmall
                            color: Theme.alpha(Theme.text, 0.06)
                            Image {
                                anchors.centerIn: parent
                                width: Theme.appIconSizeLarge; height: Theme.appIconSizeLarge
                                source: "image://icons/" + thumbnails.panel.groupIcon
                                sourceSize: Qt.size(Theme.appIconSizeLarge, Theme.appIconSizeLarge)
                                opacity: tile.minimized ? 0.5 : 1
                            }
                        }
                        Image {
                            id: picture
                            objectName: "windowThumbnailPicture"
                            readonly property real aspect: implicitHeight > 0 ? implicitWidth / implicitHeight : 1
                            anchors.centerIn: parent
                            width: Math.min(box.width, box.height * aspect)
                            height: Math.min(box.height, box.width / aspect)
                            source: tile.model.picture || ""
                            // A new picture has a new name; the old one is not wanted again, but
                            // stays until the new one has loaded.
                            cache: false
                            retainWhileLoading: true
                            smooth: true; mipmap: true
                            // Whether it has a picture to show: once one has loaded, until there
                            // is none. The first fades in over the icon standing in for it,
                            // unless it was there as the tile appeared; the next ones take its
                            // place at once.
                            property bool shown: false
                            property bool made: false
                            Component.onCompleted: made = true
                            onStatusChanged: {
                                if (status === Image.Ready && !shown) {
                                    shown = true
                                    if (made && Theme.durationNormal > 0)
                                        fadeIn.restart()
                                } else if (status !== Image.Ready && status !== Image.Loading) {
                                    shown = false
                                    fadeIn.stop()
                                    opacity = 1
                                }
                            }
                            NumberAnimation on opacity {
                                id: fadeIn
                                running: false
                                from: 0; to: 1
                                duration: Theme.durationNormal; easing.type: Theme.easing
                            }
                            visible: shown || status === Image.Ready
                            layer.enabled: Theme.effects
                            layer.effect: MultiEffect {
                                maskEnabled: true
                                maskSource: pictureMask
                                maskThresholdMin: 0.5
                                maskSpreadAtMin: 1
                            }
                        }
                        Item {
                            id: pictureMask
                            anchors.fill: picture
                            visible: false
                            layer.enabled: Theme.effects
                            Rectangle { anchors.fill: parent; radius: Theme.radiusSmall }
                        }
                        // Resting on the picture for half a second peeks at the window, as on
                        // Windows: the others fade while it shows alone where it is (the task
                        // source's peek, which a stand-in may not have). With one peeked at,
                        // another picture takes the peek over at once; leaving the pictures, or
                        // the card closing, ends it a moment later, time to cross to the next.
                        // With the keyboard on the bar, the picture it selects rests in the
                        // pointer's place.
                        HoverHandler { id: pictureHover }
                        Timer {
                            id: peekDelay
                            interval: 500
                            readonly property bool resting: (thumbnails.panel.barKeys.active ? tile.keySelected
                                                                                            : pictureHover.hovered) &&
                                                            thumbnails.open
                            readonly property var source: thumbnails.panel.taskSource
                            // The model this tile peeked through and the window, until it ends
                            // the peek, should either change meanwhile.
                            property var peeking: null
                            property int peekedId: -1
                            function peek() {
                                peeking = source
                                peekedId = tile.taskId
                                peeking.peek(peekedId)
                            }
                            function end() {
                                if (peeking)
                                    peeking.endPeek(peekedId)
                                peeking = null
                            }
                            onRestingChanged: {
                                stop()
                                peekEnd.stop()
                                if (!source || typeof source.peek !== "function")
                                    return
                                if (!resting) {
                                    if (peeking)
                                        peekEnd.start()
                                } else if (source.peekedTask >= 0) {
                                    peek()
                                } else {
                                    start()
                                }
                            }
                            onTriggered: peek()
                            Component.onDestruction: end()
                        }
                        Timer { id: peekEnd; interval: 150; onTriggered: peekDelay.end() }
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton | Qt.MiddleButton
                    onPressed: (mouse) => {
                        if (mouse.button === Qt.RightButton)
                            thumbnails.panel.openContextMenu(tile, 0, tile.taskId, tile.appId)
                    }
                    onClicked: (mouse) => {
                        if (mouse.button === Qt.MiddleButton) thumbnails.panel.taskSource.close(tile.taskId)
                    }
                }
            }
        }
    }
}
