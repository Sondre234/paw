// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw

// The dock of the macOS style: a strip floating in the middle of the panel's surface at the
// bottom of the output, as wide as what it holds. The applications button first, then the pinned
// applications, the running applications that are not pinned, and, past a line, the Trash. Each
// application is one icon (DockIcon.qml) whatever its windows, with a dot under it while it runs:
// a click starts it, or brings its windows up in turn; a right press opens its menu, and resting
// on one with several windows lists them. shell.panel_height sets its height, the icons fitting
// inside its padding, shell.panel_radius rounds it, and shell.panel_margin's bottom lifts it off
// the edge. Too many icons for the output shrink together.
Item {
    id: dock
    required property var panel
    // The dock itself, which its popups open by, and its rectangle in the panel's coordinates,
    // where the panel's surface takes the pointer.
    readonly property Item barItem: body
    readonly property rect area: Qt.rect(body.x, body.y, body.width, body.height)
    readonly property Item launcherButton: launcher
    readonly property Item trash: trashIcon
    readonly property Item pins: pinnedApps
    readonly property Item running: runningApps

    // The icons made with the dock come as they are; those that come later grow in.
    property bool settled: false
    Component.onCompleted: Qt.callLater(function() { dock.settled = true })
    // The pinned applications shown, by id: every change to the pins makes their icons anew, and
    // only one whose application was not among them comes in.
    property var shownPins: []
    function rememberPins() { shownPins = shell.pinned.map(function(app) { return app.appId }) }

    // What a click on an application's icon does: brings its windows up one at a time, the next
    // after the focused one, or starts it when it has none, bouncing until a window opens.
    function activate(icon, appId) {
        panel.closeMenus()
        if (icon.windows && icon.windows.count > 0)
            panel.taskSource.activate(icon.windows.nextTask())
        else if (appId && shell.launch(appId))
            icon.bounce(Theme.dockBounces)
    }
    // Its menu: an application's with windows is about all of them, one without offers to start it.
    function openMenu(icon, app, appId) {
        var lead = icon.windows && icon.windows.count > 0 ? icon.windows.windows[0] : null
        if (lead)
            panel.openContextMenu(icon, icon.width / 2, lead.taskId, lead.appId)
        else if (app)
            panel.openContextMenu(icon, icon.width / 2, -1, app)
    }

    Rectangle {
        id: body
        objectName: "dock"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom; anchors.bottomMargin: shell.panelMarginBottom
        height: shell.panelHeight
        // As wide as its icons, which shrink together where the output is too narrow for them.
        readonly property real room: dock.width - 2 * Theme.spacingM
        readonly property real fit: Math.min(1, (room - 2 * Theme.dockPadding) / Math.max(1, row.implicitWidth))
        width: row.implicitWidth * fit + 2 * Theme.dockPadding
        radius: Math.min(shell.panelRadius, height / 2)
        color: Theme.bar
        // A faint dark line around it, and a light one just inside.
        border.width: 1; border.color: Theme.dockOuterEdge
        Rectangle {
            anchors.fill: parent; anchors.margins: 1
            radius: Math.max(0, parent.radius - 1)
            color: "transparent"
            border.width: 1; border.color: Theme.dockInnerEdge
        }
        // A right press on the dock between its icons opens the bar's own menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: (mouse) => dock.panel.openContextMenu(body, mouse.x, -1)
        }
        Row {
            id: row
            anchors.centerIn: parent
            height: parent.height
            scale: body.fit
            spacing: Theme.dockSpacing

            DockIcon {
                id: launcher
                objectName: "dockLauncher"
                panel: dock.panel
                name: "Applications"
                checkable: false
                onClicked: dock.panel.launcherOpen = !dock.panel.launcherOpen
                // A grid of coloured squares on a dark tile.
                tile: Component {
                    Rectangle {
                        radius: width * 0.225
                        gradient: Gradient {
                            GradientStop { position: 0; color: "#62626b" }
                            GradientStop { position: 1; color: "#38383e" }
                        }
                        Grid {
                            anchors.centerIn: parent
                            columns: 3
                            spacing: parent.width * 0.07
                            Repeater {
                                model: ["#ff5f57", "#febc2e", "#28c840", "#0a84ff", "#bf5af2", "#ff9f0a",
                                        "#64d2ff", "#ff375f", "#5e5ce6"]
                                Rectangle {
                                    required property string modelData
                                    width: parent.parent.width * 0.19; height: width
                                    radius: width * 0.3
                                    color: modelData
                                }
                            }
                        }
                    }
                }
            }

            Repeater {
                id: pinnedApps
                model: shell.pinned
                Component.onCompleted: Qt.callLater(dock.rememberPins)
                onModelChanged: Qt.callLater(dock.rememberPins)
                onItemAdded: (index, item) => {
                    if (dock.settled && dock.shownPins.indexOf(item.modelData.appId) < 0)
                        item.enter()
                    Qt.callLater(dock.rememberPins)
                }
                delegate: DockIcon {
                    id: pinnedIcon
                    required property var modelData
                    objectName: "dockApp:" + modelData.appId
                    panel: dock.panel
                    iconName: modelData.icon
                    name: modelData.name
                    windows: TaskFilter { controller: shell; app: pinnedIcon.modelData.appId; sourceModel: dock.panel.taskSource }
                    groupSlot: modelData.appId
                    onClicked: dock.activate(pinnedIcon, modelData.appId)
                    function keyMenu() { dock.openMenu(pinnedIcon, pinnedIcon.modelData, pinnedIcon.modelData.appId) }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: dock.openMenu(pinnedIcon, pinnedIcon.modelData, pinnedIcon.modelData.appId)
                    }
                }
            }

            // One icon for each application running that is not pinned, by its first window.
            Repeater {
                id: runningApps
                model: TaskFilter { controller: shell; sourceModel: dock.panel.taskSource; grouped: true }
                onItemAdded: (index, item) => { if (dock.settled) item.enter() }
                delegate: DockIcon {
                    id: runningIcon
                    required property int taskId
                    required property string title
                    required property string appId
                    objectName: "dockApp:" + appId
                    panel: dock.panel
                    readonly property var record: dock.panel.appRecord(appId)
                    iconName: record ? record.icon : shell.iconFor(appId)
                    name: record ? record.name : appId || title
                    // A window without an app id stands alone.
                    windows: TaskFilter {
                        controller: shell; sourceModel: dock.panel.taskSource
                        windowApp: runningIcon.appId
                        taskId: runningIcon.appId === "" ? runningIcon.taskId : -1
                    }
                    groupWindowApp: appId
                    onClicked: dock.activate(runningIcon, "")
                    function keyMenu() { dock.openMenu(runningIcon, null, "") }
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.RightButton
                        onPressed: dock.openMenu(runningIcon, null, "")
                    }
                }
            }

            Rectangle {
                objectName: "dockSeparator"
                anchors.verticalCenter: parent.verticalCenter
                width: 1; height: Theme.dockIconSize * 0.75
                color: Theme.dockSeparator
            }

            // The Trash, full or empty, which opens in the file manager.
            DockIcon {
                id: trashIcon
                objectName: "dockTrash"
                panel: dock.panel
                iconName: shell.trashFull ? "user-trash-full" : "user-trash"
                name: "Trash"
                onClicked: { dock.panel.closeMenus(); shell.openTrash() }
            }
        }
    }
}
