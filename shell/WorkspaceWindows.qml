// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw

// The windows of one workspace of the panel's output on the card of a workspace
// (WorkspaceCard.qml), each a small copy of its picture where it lies, in the order they are
// drawn: what the task source's workspaceLayout lists, `factor` card pixels to a logical pixel of
// the output. While `live` the layer watches the workspace (watchWorkspace) and wants its windows'
// pictures through the panel's wantPicture, at the panel's workspacePictureWidth, which holds any
// window of the output; the card makes it live from halfway into the delay, so that it opens on
// them. A window keeps its copy as the list changes: one that moves or changes size glides there,
// one that comes fades and grows in, and they restack as they are raised. Until a window's
// picture has come, or without one, the application's icon on a card stands in, and the picture
// fades in over it. A stand-in source without workspaceLayout shows no windows.
Item {
    id: workspace
    required property var panel
    // The workspace, from 1; 0 for none.
    property int number: 0
    property bool live: false
    // Card pixels to a logical pixel of the output.
    property real factor: 1
    // The window under the pointer, as the task source's roles have it ({taskId, title, appId,
    // ...}), null for none; and whether a list has come for the workspace.
    readonly property var hoveredWindow: hover.window
    readonly property bool known: layout.windows !== undefined
    readonly property int count: windows.count

    readonly property var source: panel.taskSource
    readonly property bool listing: !!source && typeof source.workspaceLayout === "function"
    // The list as the source has it now, which `layout` follows.
    function current() {
        return listing && number > 0 ? source.workspaceLayout(panel.outputName, number) : ({})
    }
    readonly property var layout: {
        void (source ? source.workspaceRevision : 0)
        return current()
    }
    onLayoutChanged: sync()

    // The workspace watched, of the source and output it was asked of, until let go.
    property var watchedSource: null
    property string watchedOutput: ""
    property int watchedNumber: 0
    function rewatch() {
        var wanted = live && listing && number > 0
        if (watchedSource && (!wanted || watchedSource !== source || watchedOutput !== panel.outputName ||
                              watchedNumber !== number))
            unwatch()
        if (wanted && !watchedSource) {
            if (typeof source.watchWorkspace === "function")
                source.watchWorkspace(panel.outputName, number)
            watchedSource = source
            watchedOutput = panel.outputName
            watchedNumber = number
        }
    }
    function unwatch() {
        if (watchedSource && typeof watchedSource.unwatchWorkspace === "function")
            watchedSource.unwatchWorkspace(watchedOutput, watchedNumber)
        watchedSource = null
    }
    onLiveChanged: rewatch()
    onSourceChanged: rewatch()
    onNumberChanged: {
        rewatch()
        // Another workspace's windows start afresh, without gliding from this one's.
        windows.clear()
        settled = false
        sync()
    }
    Component.onCompleted: rewatch()
    Component.onDestruction: unwatch()

    // The copies, bottom to top, as {windowId, boxX, boxY, boxWidth, boxHeight, arriving}: kept in
    // step with the list rather than made anew, so that each window keeps its copy. Those that
    // come once the workspace's windows have shown are `arriving`, and fade in.
    ListModel { id: windows }
    property bool settled: false
    function sync() {
        // Read here rather than through `layout`, which may not have followed the number yet.
        var list = current().windows, wanted = {}, i, j
        if (list === undefined)
            return
        for (i = 0; i < list.length; ++i)
            wanted[list[i].windowId] = true
        for (j = windows.count - 1; j >= 0; --j)
            if (!wanted[windows.get(j).windowId])
                windows.remove(j)
        for (i = 0; i < list.length; ++i) {
            var entry = list[i], at = -1
            for (j = i; j < windows.count; ++j)
                if (windows.get(j).windowId === entry.windowId) {
                    at = j
                    break
                }
            var box = { boxX: entry.x, boxY: entry.y, boxWidth: entry.width, boxHeight: entry.height }
            if (at < 0) {
                windows.insert(i, { windowId: entry.windowId, boxX: box.boxX, boxY: box.boxY,
                                    boxWidth: box.boxWidth, boxHeight: box.boxHeight,
                                    arriving: settled })
                continue
            }
            if (at !== i)
                windows.move(at, i, 1)
            windows.set(i, box)
        }
        settled = true
    }

    // The window under the pointer, read by the card for its header.
    QtObject {
        id: hover
        property var window: null
        property Item item: null
    }

    Repeater {
        model: workspace.live ? windows : null
        delegate: Item {
            id: copy
            required property int index
            required property int windowId
            required property real boxX
            required property real boxY
            required property real boxWidth
            required property real boxHeight
            required property bool arriving
            objectName: "workspaceWindow"
            // Its place, eased once it is shown; never while it comes in, from the start.
            readonly property real placedX: Math.round(boxX * workspace.factor)
            readonly property real placedY: Math.round(boxY * workspace.factor)
            x: placedX; y: placedY
            width: Math.max(1, Math.round((boxX + boxWidth) * workspace.factor) - placedX)
            height: Math.max(1, Math.round((boxY + boxHeight) * workspace.factor) - placedY)
            z: index
            property bool glides: false
            Component.onCompleted: {
                glides = true
                if (arriving && Theme.durationNormal > 0)
                    comeIn.start()
                windows.setProperty(index, "arriving", false)
            }
            Behavior on x { enabled: copy.glides; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on y { enabled: copy.glides; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on width { enabled: copy.glides; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on height { enabled: copy.glides; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            ParallelAnimation {
                id: comeIn
                NumberAnimation { target: copy; property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
                NumberAnimation { target: copy; property: "scale"; from: 0.85; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
            }

            // Its window's task, found by the window's number.
            TaskFilter {
                id: task
                sourceModel: copy.windowId > 0 ? workspace.source : null
                windowId: copy.windowId
            }
            readonly property var window: task.count > 0 ? task.windows[0] : null
            readonly property int taskId: window ? window.taskId : -1
            // It wants its window's picture while it is there, at the width the panel asks
            // pictures of a workspace's windows at, as it was when it asked.
            property int wanted: -1
            property int wantedWidth: 0
            function want() {
                if (wanted >= 0)
                    workspace.panel.wantPicture(wanted, false, wantedWidth)
                wanted = taskId
                wantedWidth = workspace.panel.workspacePictureWidth
                if (wanted >= 0)
                    workspace.panel.wantPicture(wanted, true, wantedWidth)
            }
            onTaskIdChanged: want()
            Connections {
                target: workspace.panel
                function onWorkspacePictureWidthChanged() { copy.want() }
            }

            // A shadow under it, as windows have on the desktop: two soft edges a pixel out and
            // down, drawn as they are, which keeps the picture's pixels as they came.
            Rectangle {
                x: -1; y: 0; width: parent.width + 2; height: parent.height + 2
                radius: 2
                color: Qt.rgba(0, 0, 0, 0.10)
            }
            Rectangle {
                x: 0; y: 1; width: parent.width; height: parent.height
                color: Qt.rgba(0, 0, 0, 0.18)
            }
            // The application's icon on a card stands in until the picture has come, and fades
            // out under it.
            Rectangle {
                objectName: "workspaceWindowStandIn"
                anchors.fill: parent
                opacity: picture.visible ? 1 - picture.opacity : 1
                visible: opacity > 0
                color: Theme.surfaceRaised
                border.width: 1; border.color: Theme.border
                radius: Math.min(Theme.radiusSmall / 2, width / 4, height / 4)
                Image {
                    anchors.centerIn: parent
                    readonly property real side: Math.min(Theme.appIconSize, parent.width * 0.6, parent.height * 0.6)
                    width: side; height: side
                    source: copy.window ? shell.iconBase + shell.iconFor(copy.window.appId || "") : ""
                    sourceSize: Qt.size(Theme.appIconSize, Theme.appIconSize)
                    smooth: true; mipmap: true
                }
            }
            Image {
                id: picture
                objectName: "workspaceWindowPicture"
                anchors.fill: parent
                source: copy.window && copy.window.picture ? copy.window.picture : ""
                // It covers the window's rectangle, its proportions; between a window drawing at a
                // new size and the list saying so, it is cut to them rather than stretched.
                fillMode: Image.PreserveAspectCrop
                clip: true
                // A new picture has a new name; the old one is not wanted again, but stays until
                // the new one has loaded.
                cache: false
                retainWhileLoading: true
                smooth: true; mipmap: true
                // Whether it has a picture to show: once one has loaded, until there is none. The
                // first fades in over the icon standing in for it, unless it was there as the copy
                // appeared; the next ones take its place at once.
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
            }
            // The pointer on it rings it in the accent colour, and a click on the card then brings it
            // up.
            Rectangle {
                objectName: "workspaceWindowRing"
                anchors.fill: parent
                color: "transparent"
                border.width: 2; border.color: Theme.accent
                radius: Math.min(Theme.radiusSmall / 2, width / 4, height / 4)
                opacity: hovered.hovered && copy.window ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
            HoverHandler {
                id: hovered
                onHoveredChanged: {
                    if (hovered) {
                        hover.item = copy
                        hover.window = Qt.binding(function() { return copy.window })
                    } else if (hover.item === copy) {
                        hover.item = null
                        hover.window = null
                    }
                }
            }
            Component.onDestruction: {
                if (wanted >= 0)
                    workspace.panel.wantPicture(wanted, false, wantedWidth)
                if (hover.item === copy) {
                    hover.item = null
                    hover.window = null
                }
            }
        }
    }
}
