// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import "WindowMenu.js" as WindowMenu

Rectangle {
    id: desktop
    color: shell.background
    // The wallpaper, or without one the gradient (the macOS style's drawn wallpaper), in layers:
    // each new one is laid over what shows, and once it is drawn (its picture loaded, or failed and
    // the gradient in its place) it fades in, the layers under it going as it covers them. A change
    // during a fade lays the next one over both, so that nothing jumps.
    Item {
        id: backdrop
        objectName: "backdrop"
        anchors.fill: parent
        // A layer for shell.wallpaper, unless the top one is for it and still loading. The first
        // shows at once, and so does the same picture read again; `retry` is how long a picture
        // that fails to load waits before it is tried again.
        function show(retry) {
            const url = shell.wallpaper.toString()
            const top = children.length > 0 ? children[children.length - 1] : null
            if (top && top.source === url && !top.ready)
                return
            backdropLayer.createObject(backdrop, { source: url, instant: !top || top.source === url,
                                                   retry: retry || 1000 })
        }
        Component.onCompleted: show()
        Connections {
            target: shell
            // The file is read again whenever the configuration changes, as it may have.
            function onConfigChanged() { backdrop.show() }
            function onWallpaperChanged() { backdrop.show() }
        }
    }
    Component {
        id: backdropLayer
        Item {
            id: layer
            anchors.fill: parent
            property string source
            property bool instant
            property int retry
            readonly property bool failed: source !== "" && picture.status === Image.Error
            readonly property bool ready: source === "" || picture.status === Image.Ready || failed
            opacity: 0
            Image {
                id: picture
                anchors.fill: parent
                source: layer.source
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false // read from the file each time, as it may have changed
            }
            Rectangle {
                anchors.fill: parent
                visible: !Theme.macos && (layer.source === "" || layer.failed)
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(shell.background, 1.45) }
                    GradientStop { position: 1; color: shell.background }
                }
            }
            // The macOS style draws a wallpaper of its own in the gradient's place.
            Loader {
                anchors.fill: parent
                active: Theme.macos && (layer.source === "" || layer.failed)
                sourceComponent: DrawnWallpaper {}
            }
            NumberAnimation {
                id: fadeIn
                target: layer; property: "opacity"; to: 1
                duration: layer.instant ? 0 : Theme.duration(450); easing.type: Easing.InOutQuad
                // It covers those under it now.
                onFinished: {
                    for (let i = 0; i < backdrop.children.length && backdrop.children[i] !== layer; ++i)
                        backdrop.children[i].destroy()
                }
            }
            onReadyChanged: if (ready && opacity === 0 && !fadeIn.running) fadeIn.start()
            Component.onCompleted: if (ready) fadeIn.start()
            // A failed load is never retried on its own: the picture is tried again with backoff,
            // as a layer of its own, while it is the one wanted.
            onFailedChanged: if (failed) console.warn("paw wallpaper failed to load, retrying in "
                                                      + retry / 1000 + " s: " + source)
            Timer {
                running: layer.failed
                interval: layer.retry
                onTriggered: {
                    const layers = backdrop.children
                    if (layers[layers.length - 1] === layer && layer.source === shell.wallpaper.toString())
                        backdrop.show(Math.min(layer.retry * 2, 60000))
                }
            }
        }
    }
    // A right press opens the desktop's menu where it was; any other press closes it where it is,
    // rather than moving it there as it fades.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(mouse) {
            if (mouse.button === Qt.RightButton)
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
              : shell.panelVertical ? Qt.rect(shell.panelEdge === "left" ? shell.panelExtent : 0, 0,
                                              desktop.width - shell.panelExtent, desktop.height)
              : Qt.rect(0, shell.panelTop ? shell.panelExtent : 0, desktop.width, desktop.height - shell.panelExtent)
        onDismissed: open = false
        entries: [{ text: "Show desktop", icon: "minimize-2", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", icon: "palette", secondary: shell.profile,
                     submenu: WindowMenu.profileEntries() }]
                : [])
    }
}
