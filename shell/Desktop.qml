// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import "WindowMenu.js" as WindowMenu

Rectangle {
    id: desktop
    color: shell.background
    Image {
        id: wallpaper
        anchors.fill: parent
        source: shell.wallpaper
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // A failed load is never retried on its own, and a reload keeps the same source, so
        // retry with backoff and reload the file whenever the configuration changes.
        function load() {
            source = ""
            source = Qt.binding(function() { return shell.wallpaper })
        }
        onStatusChanged: {
            if (status === Image.Error) {
                console.warn("paw wallpaper failed to load, retrying in "
                             + retry.interval / 1000 + " s: " + source)
                retry.start()
            } else if (status === Image.Ready) {
                retry.interval = 1000
                shown = source
                if (previous.source.toString() !== "") fade.restart()
            }
        }
        Timer {
            id: retry
            interval: 1000
            onTriggered: {
                interval = Math.min(interval * 2, 60000)
                wallpaper.load()
            }
        }
        // A new wallpaper fades in over the one shown before (held by `previous`) once it has
        // loaded, rather than the background showing while it decodes.
        property url shown: ""
        onSourceChanged: {
            if (source.toString() !== "" && shown.toString() !== "" && source.toString() !== shown.toString()) {
                fade.stop()
                previous.source = shown
                previous.opacity = 1
            }
        }
        Connections {
            target: shell
            function onConfigChanged() {
                retry.stop()
                retry.interval = 1000
                wallpaper.load()
            }
        }
    }
    Image {
        id: previous
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        visible: source.toString() !== ""
        NumberAnimation {
            id: fade
            target: previous; property: "opacity"; to: 0; duration: Theme.duration(450); easing.type: Easing.InOutQuad
            onFinished: previous.source = ""
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: !Theme.macos && (shell.wallpaper.toString().length === 0 || wallpaper.status === Image.Error)
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.lighter(shell.background, 1.45) }
            GradientStop { position: 1; color: shell.background }
        }
    }
    // The macOS style draws a wallpaper of its own in the gradient's place.
    Loader {
        anchors.fill: parent
        active: Theme.macos && (shell.wallpaper.toString().length === 0 || wallpaper.status === Image.Error)
        sourceComponent: DrawnWallpaper {}
    }
    // A right press opens the desktop's menu where it was; any other press closes it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            menu.anchorRect = Qt.rect(mouse.x, mouse.y, 0, 0)
            menu.open = mouse.button === Qt.RightButton
        }
    }
    Column {
        x: Theme.spacingXL; y: Theme.spacingXXL + Theme.spacingS + (Theme.macos ? Theme.menuBarHeight : 0); spacing: Theme.spacingL
        Repeater {
            // Configured launchers only; applications pinned from the taskbar stay there.
            model: shell.pinned.filter(function(app) { return app.configured })
            delegate: Button {
                required property var modelData
                width: 88; height: 84
                onDoubleClicked: shell.launch(modelData.appId)
                Accessible.name: "Double-click to open " + modelData.name
                background: Rectangle { radius: Theme.radiusSmall; color: parent.hovered ? Theme.accentSubtle : "transparent"; border.color: parent.hovered ? Theme.alpha(Theme.accent, 0.33) : "transparent" }
                contentItem: Column {
                    spacing: Theme.spacingS + Theme.spacingXS
                    Image { anchors.horizontalCenter: parent.horizontalCenter; width: 40; height: 40; sourceSize: Qt.size(40, 40); source: "image://icons/" + modelData.icon }
                    Text { width: parent.width; text: modelData.name; textFormat: Text.PlainText; color: Theme.text; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                }
            }
        }
    }
    // Showing the desktop, and the appearance profiles beside their entry, as the bar's menu
    // offers them; in the room the panel leaves.
    PopupMenu {
        id: menu
        objectName: "desktopMenu"
        entryName: "desktopMenuItem"
        side: Qt.BottomEdge
        alignment: Qt.AlignLeft
        gap: 0
        bounds: Theme.macos ? Qt.rect(0, Theme.menuBarHeight, desktop.width, desktop.height - Theme.menuBarHeight - shell.panelExtent)
                            : Qt.rect(0, shell.panelTop ? shell.panelExtent : 0, desktop.width, desktop.height - shell.panelExtent)
        onDismissed: open = false
        entries: [{ text: "Show desktop", icon: "minimize-2", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", icon: "palette", secondary: shell.profile,
                     submenu: WindowMenu.profileEntries() }]
                : [])
    }
}
