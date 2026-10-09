// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The wallpaper button's popup: thumbnails of the pictures in shell.wallpapers, by subfolder,
// with a filter; clicking one shows it at once and keeps the picker open to try another.
PopupCard {
    id: wallpaperPicker
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "wallpaperPicker"
    open: panel.audioPopup === "wallpapers"
    // "" shows every folder.
    property string folder: ""
    readonly property var folders: {
        var seen = [], list = shell.wallpapers
        for (var i = 0; i < list.length; ++i)
            if (seen.indexOf(list[i].folder) < 0) seen.push(list[i].folder)
        return seen
    }
    readonly property var shown: {
        var words = filter.text.toLowerCase().split(/\s+/).filter(function(w) { return w.length > 0 })
        var folder = wallpaperPicker.folder
        return shell.wallpapers.filter(function(w) {
            if (folder !== "" && w.folder !== folder) return false
            var text = (w.folder + "/" + w.name).toLowerCase()
            return words.every(function(word) { return text.indexOf(word) >= 0 })
        })
    }
    onOpened: shell.findWallpapers()
    initialFocus: filter
    implicitWidth: 880
    // As tall as when it shared the bar's surface, which grew to at most 560 pixels.
    implicitHeight: Math.max(220, Math.min(500, Math.min(560, panel.popupLayer.height) - barItem.height - shell.panelMarginTop - shell.panelMarginBottom - 16))
    anchorRect: panel.barAnchor(panel.audioPopupX, 0)
    side: panel.popupSide
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 10; spacing: 8
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Text {
                text: "Wallpapers"; color: Theme.text; font.pixelSize: shell.fontSize + 1; font.bold: true; font.family: Theme.fontFamily
            }
            Text {
                Layout.fillWidth: true
                text: wallpaperPicker.shown.length + (wallpaperPicker.shown.length === 1 ? " picture" : " pictures")
                elide: Text.ElideRight
                color: Theme.textMuted; font.pixelSize: shell.fontSize - 1; font.family: Theme.fontFamily
            }
            TextField {
                id: filter
                objectName: "wallpaperFilter"
                Layout.preferredWidth: 220; Layout.preferredHeight: 30
                placeholderText: "Filter"
                color: Theme.text; placeholderTextColor: Theme.textMuted
                font.pixelSize: shell.fontSize; font.family: Theme.fontFamily
                background: Rectangle { radius: 6; color: Theme.surfaceRaised; border.color: filter.activeFocus ? Theme.accent : "transparent" }
                Keys.onEscapePressed: panel.closeMenus()
                Keys.onReturnPressed: if (wallpaperPicker.shown.length > 0) shell.pickWallpaper(wallpaperPicker.shown[0].path)
            }
            Button {
                id: shuffle
                objectName: "wallpaperShuffle"
                Layout.preferredWidth: 30; Layout.preferredHeight: 30
                enabled: wallpaperPicker.shown.length > 0
                Accessible.name: "Random wallpaper"
                onClicked: shell.pickWallpaper(wallpaperPicker.shown[Math.floor(Math.random() * wallpaperPicker.shown.length)].path)
                background: Rectangle { radius: 6; color: shuffle.hovered ? Theme.hover : "transparent" }
                contentItem: Item { Icon { anchors.centerIn: parent; name: "shuffle"; size: 16 } }
            }
        }
        // The subfolders, as tabs.
        ListView {
            id: folderTabs
            Layout.fillWidth: true; Layout.preferredHeight: 28
            visible: wallpaperPicker.folders.length > 1
            orientation: ListView.Horizontal; spacing: 6; clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: [""].concat(wallpaperPicker.folders)
            delegate: Button {
                id: folderTab
                required property string modelData
                readonly property bool current: modelData === wallpaperPicker.folder
                height: 28
                onClicked: wallpaperPicker.folder = modelData
                background: Rectangle {
                    radius: 14
                    color: folderTab.current ? Theme.accent : (folderTab.hovered ? Theme.selected : Theme.hover)
                }
                contentItem: Text {
                    leftPadding: 6; rightPadding: 6
                    text: folderTab.modelData === "" ? "All" : folderTab.modelData
                    verticalAlignment: Text.AlignVCenter
                    color: folderTab.current ? Theme.textOnAccent : Theme.text
                    font.pixelSize: shell.fontSize - 1; font.family: Theme.fontFamily
                }
            }
            WheelHandler {
                // Qt takes the whole pointer for a touchpad once the compositor offers gestures
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: (event) => {
                    var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                    folderTabs.contentX = Math.max(0, Math.min(folderTabs.contentWidth - folderTabs.width, folderTabs.contentX - delta))
                }
            }
        }
        GridView {
            id: wallpaperGrid
            objectName: "wallpaperGrid"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(2, Math.floor(width / 200))
            cellWidth: Math.floor(width / columns)
            cellHeight: Math.floor((cellWidth - 8) * 9 / 16) + 8
            model: wallpaperPicker.shown
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            Text {
                anchors.centerIn: parent
                visible: wallpaperGrid.count === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap
                text: shell.wallpapers.length === 0 ? "No pictures in " + shell.wallpaperFolder : "Nothing matches"
                color: Theme.textMuted; font.pixelSize: shell.fontSize; font.family: Theme.fontFamily
            }
            delegate: Button {
                id: thumb
                objectName: "wallpaperItem"
                required property var modelData
                readonly property bool current: modelData.path === shell.wallpaperFile
                width: wallpaperGrid.cellWidth; height: wallpaperGrid.cellHeight
                Accessible.name: modelData.name + (current ? ", in use" : "")
                onClicked: shell.pickWallpaper(modelData.path)
                background: Item {}
                contentItem: Item {
                    Rectangle {
                        anchors.fill: parent; anchors.margins: 4
                        radius: 6
                        color: Theme.surfaceRaised
                        Image {
                            anchors.fill: parent; anchors.margins: 2
                            source: "image://thumbs/" + encodeURIComponent(thumb.modelData.path)
                            sourceSize: Qt.size(256, 256)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            smooth: true
                            opacity: status === Image.Ready ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 150 } }
                        }
                        // The name, over the picture while hovered (bar tooltips stay
                        // hidden while a popup is open).
                        Rectangle {
                            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.margins: 2
                            height: nameText.implicitHeight + 6
                            visible: thumb.hovered
                            color: Theme.alpha(Theme.surface, 0.85)
                            Text {
                                id: nameText
                                anchors.fill: parent; anchors.leftMargin: 6; anchors.rightMargin: 6
                                verticalAlignment: Text.AlignVCenter; elide: Text.ElideMiddle
                                text: thumb.modelData.name
                                color: Theme.text; font.pixelSize: shell.fontSize - 2; font.family: Theme.fontFamily
                            }
                        }
                        // The border sits over the picture: the software renderer
                        // cannot clip it to rounded corners.
                        Rectangle {
                            anchors.fill: parent
                            radius: 6; color: "transparent"
                            border.width: thumb.current ? 3 : (thumb.hovered ? 2 : 0)
                            border.color: thumb.current ? Theme.accent : Theme.alpha(Theme.text, 0.7)
                        }
                    }
                }
            }
        }
    }
}
