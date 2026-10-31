// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The wallpaper button's popup: thumbnails of the pictures in shell.wallpapers, by subfolder,
// with a filter; clicking one shows it at once and keeps the picker open to try another. With two
// monitors or more it shows on all of them, or on this one alone. Beside a bar along a side it is
// as long as the bar, its folders in a column down its side and its filter on a line of its own.
PopupCard {
    id: wallpaperPicker
    required property var panel
    parent: panel.popupLayer
    objectName: "wallpaperPicker"
    open: panel.audioPopup === "wallpapers"
    // "" shows every folder.
    property string folder: ""
    // Beside a bar along a side, where the room is the screen's height rather than its width.
    readonly property bool tall: panel.vertical
    readonly property var folders: {
        var seen = [], list = shell.wallpapers
        for (var i = 0; i < list.length; ++i)
            if (seen.indexOf(list[i].folder) < 0) seen.push(list[i].folder)
        return seen
    }
    // shell.wallpaperFoldersUsed as the picker opened: the tabs keep their places until it closes,
    // so that none moves from under the pointer.
    property var foldersUsedAtOpen: []
    // The tabs, "" for All: those filtered by before first, the last one leftmost, then the rest
    // in order. Pictures directly in the folder show under All.
    readonly property var tabs: {
        var all = [""].concat(folders.filter(function(f) { return f !== "" }))
        var used = foldersUsedAtOpen.filter(function(f) { return all.indexOf(f) >= 0 })
        return used.concat(all.filter(function(f) { return used.indexOf(f) < 0 }))
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
    // Where a picture picked goes: "all" monitors, or this "monitor" alone, as it has now.
    property string scope: "all"
    // Whether this monitor had a wallpaper of its own as the picker opened, which offers the
    // choice with one monitor too, until it closes.
    property bool ownAtOpen: false
    function pick(path) {
        if (scope === "monitor")
            shell.pickWallpaperOn(path, panel.outputName)
        else
            shell.pickWallpaper(path)
    }
    onOpened: {
        shell.findWallpapers()
        ownAtOpen = panel.shellView.ownWallpaper
        scope = ownAtOpen ? "monitor" : "all"
        foldersUsedAtOpen = shell.wallpaperFoldersUsed
        folderTabs.positionViewAtBeginning()
    }
    initialFocus: filter
    implicitWidth: 880
    // As tall as when it shared the bar's surface, which grew to at most 560 pixels; beside a bar
    // along a side, the bar's length, lined up with it.
    implicitHeight: tall ? panel.bar.height
        : Math.max(220, Math.min(500, Math.min(560, panel.popupLayer.height) - panel.statusBar.height - shell.panelMarginTop - shell.panelMarginBottom - 16))
    anchorRect: tall ? panel.barAnchor(panel.barTop, panel.bar.height) : panel.barAnchor(panel.audioPopupX, 0)
    side: panel.popupSide
    // The header across the top, and under it the folders above the pictures, or in a tall picker
    // beside them.
    GridLayout {
        anchors.fill: parent; anchors.margins: 10
        columns: wallpaperPicker.tall ? 2 : 1
        rowSpacing: 8; columnSpacing: 8
        // The title, the count and the monitor choice, then the filter and the shuffle: on one line,
        // or in a tall picker the filter on a line of its own across it.
        GridLayout {
            Layout.fillWidth: true; Layout.columnSpan: wallpaperPicker.tall ? 2 : 1
            columns: wallpaperPicker.tall ? 1 : 2
            rowSpacing: 8; columnSpacing: 8
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
                // Every monitor or this one alone, shown with two monitors or more, or when this one
                // had a wallpaper of its own as the picker opened.
                Row {
                    id: scopeChoice
                    objectName: "wallpaperScope"
                    visible: Qt.application.screens.length > 1 || wallpaperPicker.ownAtOpen
                    spacing: 4
                    Repeater {
                        model: [{ scope: "all", text: "All monitors", name: "Use on all monitors" },
                                { scope: "monitor", text: "This monitor only", name: "Use on this monitor only" }]
                        delegate: Button {
                            id: scopeButton
                            required property var modelData
                            readonly property bool current: wallpaperPicker.scope === modelData.scope
                            objectName: modelData.scope === "all" ? "wallpaperScopeAll" : "wallpaperScopeMonitor"
                            height: 30
                            Accessible.name: modelData.name
                            Accessible.checkable: true
                            Accessible.checked: current
                            onClicked: wallpaperPicker.scope = modelData.scope
                            background: Rectangle {
                                radius: 15
                                color: scopeButton.current ? Theme.accent : (scopeButton.hovered ? Theme.selected : Theme.hover)
                            }
                            contentItem: Text {
                                leftPadding: 8; rightPadding: 8
                                text: scopeButton.modelData.text
                                verticalAlignment: Text.AlignVCenter
                                color: scopeButton.current ? Theme.textOnAccent : Theme.text
                                font.pixelSize: shell.fontSize - 1; font.family: Theme.fontFamily
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: wallpaperPicker.tall; spacing: 8
                TextField {
                    id: filter
                    objectName: "wallpaperFilter"
                    Layout.fillWidth: wallpaperPicker.tall
                    Layout.preferredWidth: 220; Layout.preferredHeight: 30
                    placeholderText: "Filter"
                    color: Theme.text; placeholderTextColor: Theme.textMuted
                    font.pixelSize: shell.fontSize; font.family: Theme.fontFamily
                    background: Rectangle { radius: 6; color: Theme.surfaceRaised; border.color: filter.activeFocus ? Theme.accent : "transparent" }
                    Keys.onEscapePressed: panel.closeMenus()
                    Keys.onReturnPressed: if (wallpaperPicker.shown.length > 0) wallpaperPicker.pick(wallpaperPicker.shown[0].path)
                }
                Button {
                    id: shuffle
                    objectName: "wallpaperShuffle"
                    Layout.preferredWidth: 30; Layout.preferredHeight: 30
                    enabled: wallpaperPicker.shown.length > 0
                    Accessible.name: "Random wallpaper"
                    onClicked: wallpaperPicker.pick(wallpaperPicker.shown[Math.floor(Math.random() * wallpaperPicker.shown.length)].path)
                    background: Rectangle { radius: 6; color: shuffle.hovered ? Theme.hover : "transparent" }
                    contentItem: Item { Icon { anchors.centerIn: parent; name: "shuffle"; size: 16 } }
                }
            }
        }
        // The subfolders, as tabs, the one filtered by last leftmost as the picker opens; in a tall
        // picker a column of them, the last topmost, a long path elided in the middle and shown
        // whole on hover.
        ListView {
            id: folderTabs
            Layout.fillWidth: !wallpaperPicker.tall; Layout.fillHeight: wallpaperPicker.tall
            Layout.preferredWidth: 180; Layout.preferredHeight: 28
            visible: wallpaperPicker.folders.length > 1
            orientation: wallpaperPicker.tall ? ListView.Vertical : ListView.Horizontal
            spacing: wallpaperPicker.tall ? 4 : 6; clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: wallpaperPicker.tabs
            delegate: Button {
                id: folderTab
                objectName: "wallpaperFolder"
                required property string modelData
                readonly property bool current: modelData === wallpaperPicker.folder
                width: wallpaperPicker.tall ? folderTabs.width : implicitWidth
                height: 28
                onClicked: {
                    wallpaperPicker.folder = modelData
                    shell.useWallpaperFolder(modelData)
                }
                background: Rectangle {
                    radius: 14
                    color: folderTab.current ? Theme.accent : (folderTab.hovered ? Theme.selected : Theme.hover)
                }
                contentItem: Text {
                    id: folderName
                    leftPadding: 6; rightPadding: 6
                    text: folderTab.modelData === "" ? "All" : folderTab.modelData
                    verticalAlignment: Text.AlignVCenter
                    elide: wallpaperPicker.tall ? Text.ElideMiddle : Text.ElideNone
                    color: folderTab.current ? Theme.textOnAccent : Theme.text
                    font.pixelSize: shell.fontSize - 1; font.family: Theme.fontFamily
                }
                BarTip {
                    panel: wallpaperPicker.panel; owner: folderTab
                    visible: folderTab.hovered && folderName.truncated
                    text: folderTab.modelData
                }
            }
            WheelHandler {
                // The row of tabs turns the wheel sideways; the column scrolls with it as it is.
                enabled: !wallpaperPicker.tall
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
                readonly property bool current: modelData.path === panel.shellView.wallpaperFile
                width: wallpaperGrid.cellWidth; height: wallpaperGrid.cellHeight
                Accessible.name: modelData.name + (current ? ", in use" : "")
                onClicked: wallpaperPicker.pick(modelData.path)
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
