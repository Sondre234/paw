// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import "WindowMenu.js" as WindowMenu

Rectangle {
    id: desktop
    // Set by the view: itself, which says where its output's bar is.
    required property var shellView
    color: shell.background
    // What the bars leave of the desktop, in its coordinates.
    readonly property rect free: {
        const panel = shellView.panelExtent
        return Theme.macos ? Qt.rect(0, Theme.menuBarHeight, width, height - Theme.menuBarHeight - panel)
             : shellView.panelVertical ? Qt.rect(shellView.panelEdge === "left" ? panel : 0, 0, width - panel, height)
             : Qt.rect(0, shellView.panelEdge === "top" ? panel : 0, width, height - panel)
    }
    // The wallpaper of this output (its own, else every monitor's), or without one the gradient
    // (the macOS style's drawn wallpaper), in layers: each new one is laid over what shows, and
    // once it is drawn (its picture loaded, or failed and the gradient in its place) it fades in,
    // the layers under it going as it covers them. A change during a fade lays the next one over
    // both, so that nothing jumps. A new shell.wallpaper_mode comes in the same way.
    Item {
        id: backdrop
        objectName: "backdrop"
        anchors.fill: parent
        // Whether `layer` shows the output's wallpaper as shell.wallpaper_mode says (the gradient
        // in any).
        function shows(layer) {
            const url = shellView.wallpaper.toString()
            return layer.source === url && (url === "" || layer.mode === shell.wallpaperMode)
        }
        // A layer for the output's wallpaper, unless the top one is for it already: a reload that
        // keeps the wallpaper reads and decodes nothing again. The first shows at once, and so
        // does a picture that failed, tried again after `retry` milliseconds, which doubles each
        // time.
        function show(retry) {
            const top = children.length > 0 ? children[children.length - 1] : null
            if (top && shows(top) && !retry)
                return
            backdropLayer.createObject(backdrop, { source: shellView.wallpaper.toString(),
                                                   mode: shell.wallpaperMode,
                                                   instant: !top || shows(top), retry: retry || 1000 })
        }
        Component.onCompleted: show()
        Connections {
            target: shell
            function onConfigChanged() { backdrop.show(); glassCapture.restart() }
            function onWallpaperChanged() { backdrop.show() }
        }
        // Whether what shows is all there is to show: the top layer drawn and faded in.
        function settled() {
            const top = children.length > 0 ? children[children.length - 1] : null
            return top !== null && top.ready && top.opacity === 1
        }
        onWidthChanged: glassCapture.restart()
        onHeightChanged: glassCapture.restart()
    }
    // The frosted glass (shell.blur) of the shell's surfaces on this output shows this wallpaper,
    // blurred, as it is drawn here: taken once a new one has faded in, and as the configuration
    // changes what the gradient or the drawn wallpaper looks like. A fade under way takes it as it
    // ends instead.
    Timer {
        id: glassCapture
        interval: 0
        onTriggered: if (shell.blur && backdrop.settled()) shell.glass.capture(backdrop, shell.background)
    }
    Component {
        id: backdropLayer
        Item {
            id: layer
            anchors.fill: parent
            property string source
            property string mode
            property bool instant
            property int retry
            readonly property bool failed: source !== "" && picture.status === Image.Error
            readonly property bool ready: source === "" || picture.status === Image.Ready || failed
            opacity: 0
            WallpaperPicture {
                id: picture
                anchors.fill: parent
                source: layer.source
                mode: layer.mode
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
                    glassCapture.restart()
                }
            }
            // A picture shown already that is read again (as the window learns its scale, on a
            // monitor not at 1) is a new picture for the glass.
            onReadyChanged: {
                if (ready && opacity === 0 && !fadeIn.running)
                    fadeIn.start()
                else if (ready && !fadeIn.running)
                    glassCapture.restart()
            }
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
                    if (layers[layers.length - 1] === layer && backdrop.shows(layer))
                        backdrop.show(Math.min(layer.retry * 2, 60000))
                }
            }
        }
    }
    // The clock (shell.desktop_clock), over the wallpaper but outside the backdrop, so that the
    // frosted glass shows the wallpaper alone.
    DesktopClock {
        anchors.fill: parent
        area: desktop.free
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
        bounds: desktop.free
        onDismissed: open = false
        entries: [{ text: "Show desktop", icon: "minimize-2", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", icon: "palette", secondary: shell.profile,
                     submenu: WindowMenu.profileEntries() }]
                : [])
    }
}
