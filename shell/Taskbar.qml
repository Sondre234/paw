// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import Paw

// The taskbar (shell.style "taskbar"): the bar along the panel's edge, as Windows 11 has it, with
// the start button, the pinned applications and the windows in one row, then the workspaces, the widgets
// shell.widgets puts on the bar, Quick Settings, the clock and the show-desktop sliver. It fills
// the panel's surface and draws the bar inset by shell.panel_margin. Along a side of the output it
// stands upright, the row a column from the top down.
Item {
    id: taskbar
    required property var panel
    readonly property bool vertical: panel.vertical
    // What the panel reaches: the bar itself, which the popups open by, and the buttons that open
    // them, as TopMenuBar.qml names its own.
    readonly property Item barItem: bar
    readonly property Item tasks: taskList
    readonly property Item trayRow: tray
    readonly property Item clock: clockButton
    readonly property Item volume: audioWidget
    readonly property Item network: networkWidget
    readonly property Item profiles: profilesButton
    readonly property Item wallpapers: wallpapersButton
    readonly property Item tiling: tilingButton
    readonly property Item quickSettings: quickButton
    // Inset from the output's edges or rounded, the bar floats.
    readonly property bool floating: shell.panelRadius > 0 || shell.panelMarginLeft > 0 ||
                                     shell.panelMarginRight > 0 || shell.panelMarginTop > 0 ||
                                     shell.panelMarginBottom > 0
    // A floating bar docks while a window reaches the edge of the output's usable area it is
    // along (a maximized window, any tile, one snapped against it), as an island over
    // a window flush beneath it looks out of place: it fills the strip it reserves from edge to
    // edge, squared, its frame and shadow gone but for a line along its inner edge, its buttons
    // where they were; and floats again once no window does (shell.panel_dock = false: it
    // always floats; "maximized": only a window reaching every edge, on a monitor that does not
    // tile, docks it). `dock` eases from 0 to 1.
    readonly property var outputState: shell.workspaces[panel.outputName] || {}
    readonly property var touchedEdges: outputState.touched || []
    readonly property bool docked: floating && (shell.panelDock === "always" ?
        touchedEdges.indexOf(panel.edge) >= 0 :
        shell.panelDock === "maximized" && !outputState.tiling && touchedEdges.length === 4)
    property real dock: docked ? 1 : 0
    Behavior on dock { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }

    // Framed as windows are (shell.frame), a floating bar casts an unfocused window's shadow, into
    // the margins around it.
    Loader {
        objectName: "barShadow"
        active: Theme.effects && taskbar.floating && Theme.barShadow.a > 0 && taskbar.dock < 1
        x: bar.x; y: bar.y; width: bar.width; height: bar.height
        opacity: 1 - taskbar.dock
        sourceComponent: RectangularShadow {
            radius: bar.radius
            blur: Theme.shadowBlur
            offset: Qt.vector2d(Theme.shadowOffsetX, Theme.shadowOffset)
            color: Theme.barShadow
        }
    }
    Rectangle {
        id: bar
        objectName: "bar"
        // Placed and sized rather than anchored, so that moving it from one edge to another never
        // leaves opposite edges anchored, deciding its size for a moment. Across the panel's
        // surface it is shell.panel_height thick, and grows over the margins as it docks.
        readonly property real floats: 1 - taskbar.dock
        x: !taskbar.vertical || taskbar.panel.edge === "left" ? shell.panelMarginLeft * floats
                                                             : parent.width - width - shell.panelMarginRight * floats
        y: taskbar.vertical || taskbar.panel.onTop ? shell.panelMarginTop * floats
                                                   : parent.height - height - shell.panelMarginBottom * floats
        width: taskbar.vertical ? shell.panelHeight + (shell.panelMarginLeft + shell.panelMarginRight) * taskbar.dock
                                : parent.width - (shell.panelMarginLeft + shell.panelMarginRight) * floats
        height: taskbar.vertical ? parent.height - (shell.panelMarginTop + shell.panelMarginBottom) * floats
                                 : shell.panelHeight + (shell.panelMarginTop + shell.panelMarginBottom) * taskbar.dock
        color: Theme.bar
        radius: shell.panelRadius * (1 - taskbar.dock)
        // A floating bar gets an outline (as windows have, framed as they are); one along an edge,
        // or docked, a line along its inner edge.
        border.width: taskbar.floating ? Theme.outlineWidth : 0
        border.color: Qt.rgba(Theme.outline.r, Theme.outline.g, Theme.outline.b,
                              Theme.outline.a * (1 - taskbar.dock))
        // Along the bar's inner edge, the one towards the middle of the output.
        Rectangle {
            objectName: "barEdge"
            visible: !taskbar.floating || taskbar.dock > 0
            opacity: taskbar.floating ? taskbar.dock : 1
            readonly property int thickness: taskbar.floating ? Math.max(1, Theme.outlineWidth) : 1
            x: taskbar.panel.edge === "left" ? parent.width - thickness : 0
            y: taskbar.panel.onTop ? parent.height - thickness : 0
            width: taskbar.vertical ? thickness : parent.width
            height: taskbar.vertical ? parent.height : thickness
            color: taskbar.floating ? Theme.outline : Theme.border
        }
        // Right-clicking the bar anywhere but on a task opens the bar's own menu. With
        // shell.panel_draggable, pressing it there and moving drags the bar to another edge
        // (panel.startBarDrag), once a press; the compositor then takes the pointer, and the
        // press is let go of here.
        MouseArea {
            anchors.fill: parent
            enabled: !taskbar.panel.barHeld
            acceptedButtons: shell.panelDraggable ? Qt.LeftButton | Qt.RightButton : Qt.RightButton
            property point pressedAt
            property bool dragging: false
            onPressed: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    taskbar.panel.openContextMenu(bar, taskbar.vertical ? mouse.y : mouse.x, -1)
                } else {
                    pressedAt = Qt.point(mouse.x, mouse.y)
                    dragging = false
                }
            }
            onPositionChanged: (mouse) => {
                if (!dragging && (mouse.buttons & Qt.LeftButton) &&
                    Math.hypot(mouse.x - pressedAt.x, mouse.y - pressedAt.y) >= Qt.styleHints.startDragDistance) {
                    dragging = true
                    taskbar.panel.startBarDrag()
                }
            }
        }
        // Scrolling the bar anywhere its widgets leave the wheel alone pages through this
        // output's workspaces: a wheel notch (or a touchpad's worth of travel) moves one,
        // stopping at either end, where the windows nudge a little the way the slide would take
        // them and the pill toward the workspace that is not there; down or right goes to the next.
        NotchWheel {
            onNotched: (steps) => {
                var current = workspaceIndicator.workspaceState.current
                var target = Math.max(1, Math.min(shell.workspaceCount, current - steps))
                if (target === current)
                    workspaceIndicator.nudge(steps < 0 ? 1 : -1)
                else
                    workspaceIndicator.show(target)
            }
        }
        // A row, or along a side a column as wide as a button (the bar but a small margin either
        // side, as the row is as tall as one).
        GridLayout {
            // Docked, inset by the margins the bar grew over, so that its buttons stay put.
            anchors.fill: parent
            anchors.leftMargin: (taskbar.vertical ? Theme.spacingS : Theme.spacingM) + shell.panelMarginLeft * taskbar.dock
            anchors.rightMargin: (taskbar.vertical ? Theme.spacingS : Theme.spacingM) + shell.panelMarginRight * taskbar.dock
            anchors.topMargin: (taskbar.vertical ? Theme.spacingM : 0) + shell.panelMarginTop * taskbar.dock
            anchors.bottomMargin: (taskbar.vertical ? Theme.spacingM : 0) + shell.panelMarginBottom * taskbar.dock
            rowSpacing: Theme.spacingS; columnSpacing: Theme.spacingS
            flow: taskbar.vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
            FlatButton {
                id: start
                Layout.preferredWidth: panel.barItemWidth; Layout.preferredHeight: panel.barItemHeight
                active: taskbar.panel.launcherOpen
                onClicked: taskbar.panel.launcherOpen = !taskbar.panel.launcherOpen
                Accessible.name: "Applications"
                BarTip { panel: taskbar.panel; owner: start; text: "Start" }
                // Four squares in the accent colour, or the picture shell.start_icon names, which
                // shrink a little while pressed as an application's icon does.
                contentItem: Item {
                    Item {
                        anchors.centerIn: parent
                        width: startIcon.visible ? startIcon.width : squares.width
                        height: startIcon.visible ? startIcon.height : squares.height
                        scale: start.pressed ? Theme.pressScale : 1
                        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        Grid {
                            id: squares
                            anchors.centerIn: parent; columns: 2; spacing: 3
                            visible: !startIcon.visible
                            Repeater { model: 4; Rectangle { width: 9; height: 9; radius: 2; color: Theme.accent } }
                        }
                        Image {
                            id: startIcon
                            objectName: "startIcon"
                            anchors.centerIn: parent
                            readonly property int side: Math.min(26, start.height - 4)
                            width: side; height: side
                            sourceSize: Qt.size(side * 2, side * 2)
                            source: shell.startIcon
                            fillMode: Image.PreserveAspectFit
                            smooth: true; mipmap: true
                            // A picture that does not load leaves the squares.
                            visible: status === Image.Ready
                        }
                    }
                }
            }
            TaskList { id: taskList; panel: taskbar.panel }
            WorkspaceIndicator { id: workspaceIndicator; panel: taskbar.panel }
            BindingMode { panel: taskbar.panel }
            WallpapersButton { id: wallpapersButton; panel: taskbar.panel }
            ProfilesButton { id: profilesButton; panel: taskbar.panel }
            TilingButton { id: tilingButton; panel: taskbar.panel }
            // The system tray: the status icons of applications, in the order they appeared.
            // Passive ones stay hidden, and so does the tray when none is left.
            Grid {
                id: tray
                objectName: "tray"
                visible: shell.widgets.tray && shell.tray.shown > 0
                Layout.alignment: Qt.AlignCenter
                columns: taskbar.vertical ? 1 : Math.max(1, trayItems.count)
                Repeater {
                    id: trayItems
                    model: shell.tray
                    delegate: TrayButton { panel: taskbar.panel }
                }
            }
            NotificationBell { id: bell; panel: taskbar.panel }
            NetworkWidget { id: networkWidget; panel: taskbar.panel }
            BatteryWidget { panel: taskbar.panel }
            VolumeButton { id: audioWidget; panel: taskbar.panel }
            KeyboardLayout { panel: taskbar.panel }
            QuickSettingsButton { id: quickButton; panel: taskbar.panel }
            ClockButton { id: clockButton; panel: taskbar.panel }
            // A sliver at the bar's end, its line taking the accent colour under the pointer.
            Button {
                id: showDesktopButton
                readonly property real sliver: Theme.spacingL + Theme.spacingXS
                Layout.preferredWidth: taskbar.vertical ? -1 : sliver; Layout.fillHeight: !taskbar.vertical
                Layout.preferredHeight: taskbar.vertical ? sliver : -1; Layout.fillWidth: taskbar.vertical
                onClicked: { taskbar.panel.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                BarTip { panel: taskbar.panel; owner: showDesktopButton; text: "Show desktop" }
                background: Rectangle {
                    x: taskbar.vertical ? 0 : parent.width - width
                    y: taskbar.vertical ? parent.height - height : 0
                    width: taskbar.vertical ? parent.width : 3
                    height: taskbar.vertical ? 3 : parent.height
                    color: showDesktopButton.pressed ? Theme.accentHover : showDesktopButton.hovered ? Theme.accent : Theme.border
                    Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                }
            }
        }
        Rectangle {
            visible: shell.error.length > 0
            anchors.fill: parent; anchors.margins: Theme.spacingS
            color: Theme.dangerSurface; radius: Theme.radiusSmall
            Text { anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: Theme.spacingL; text: shell.error; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
            CloseButton {
                id: dismiss
                anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: "Dismiss"
                onClicked: shell.clearError()
            }
        }
        // With shell.panel_draggable, the bar's inner edge resizes it, as on Windows with the
        // taskbar unlocked: a strip as thick as the margin the buttons leave there, under a resize
        // cursor. A press on it has the compositor take the pointer (panel.startBarResize), and
        // the bar follows it from there until it is let go. The strip is made anew once the drag
        // is over: Qt never hears the button let go, which the compositor keeps, and a MouseArea
        // still pressed would take no new press.
        Loader {
            active: shell.panelDraggable && !taskbar.panel.barHeld
            readonly property int thickness: Theme.spacingS
            x: taskbar.panel.edge === "left" ? parent.width - thickness : 0
            y: taskbar.panel.onTop ? parent.height - thickness : 0
            width: taskbar.vertical ? thickness : parent.width
            height: taskbar.vertical ? parent.height : thickness
            sourceComponent: MouseArea {
                objectName: "barResizeEdge"
                cursorShape: taskbar.vertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
                onPressed: (mouse) => taskbar.panel.startBarResize(mapToItem(null, mouse.x, mouse.y))
            }
        }
    }
}
