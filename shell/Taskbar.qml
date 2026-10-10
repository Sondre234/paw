// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Paw

// The taskbar (shell.style "taskbar"): the bar along the panel's edge, as Windows 11 has it, with
// the start button, the pinned applications and the windows in one row, then the workspaces, the widgets
// shell.widgets puts on the bar, Quick Settings, the clock and the show-desktop sliver. It fills
// the panel's surface and draws the bar inset by shell.panel_margin.
Item {
    id: taskbar
    required property var panel
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
    readonly property Item quickSettings: quickButton
    // Inset from the output's edges or rounded, the bar floats.
    readonly property bool floating: shell.panelRadius > 0 || shell.panelMarginLeft > 0 ||
                                     shell.panelMarginRight > 0 || shell.panelMarginTop > 0 ||
                                     shell.panelMarginBottom > 0

    Rectangle {
        id: bar
        objectName: "bar"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.leftMargin: shell.panelMarginLeft; anchors.rightMargin: shell.panelMarginRight
        anchors.bottom: taskbar.panel.onTop ? undefined : parent.bottom
        anchors.top: taskbar.panel.onTop ? parent.top : undefined
        anchors.bottomMargin: shell.panelMarginBottom; anchors.topMargin: shell.panelMarginTop
        height: shell.panelHeight
        color: Theme.bar
        radius: shell.panelRadius
        // A floating bar gets an outline; a docked one a line along its inner edge.
        border.width: taskbar.floating ? 1 : 0
        border.color: Theme.border
        Rectangle {
            visible: !taskbar.floating
            y: taskbar.panel.onTop ? parent.height - 1 : 0
            width: parent.width; height: 1; color: Theme.border
        }
        // Right-clicking the bar anywhere but on a task opens the bar's own menu.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onPressed: (mouse) => taskbar.panel.openContextMenu(bar, mouse.x, -1)
        }
        // Scrolling the bar anywhere its widgets leave the wheel alone pages through this
        // output's workspaces: a wheel notch (or a touchpad's worth of travel) moves one,
        // stopping at either end; down or right goes to the next.
        NotchWheel {
            onNotched: (steps) => {
                var target = workspaceIndicator.workspaceState.current - steps
                workspaceIndicator.show(Math.max(1, Math.min(shell.workspaceCount, target)))
            }
        }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: Theme.spacingM; anchors.rightMargin: Theme.spacingM
            spacing: Theme.spacingS
            FlatButton {
                id: start
                Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
                active: taskbar.panel.launcherOpen
                onClicked: taskbar.panel.launcherOpen = !taskbar.panel.launcherOpen
                Accessible.name: "Applications"
                BarTip { panel: taskbar.panel; owner: start; text: "Start" }
                // Four squares in the accent colour, which shrink a little while pressed as an
                // application's icon does.
                contentItem: Item {
                    Grid {
                        anchors.centerIn: parent; columns: 2; spacing: 3
                        scale: start.pressed ? Theme.pressScale : 1
                        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        Repeater { model: 4; Rectangle { width: 9; height: 9; radius: 2; color: Theme.accent } }
                    }
                }
            }
            TaskList { id: taskList; panel: taskbar.panel }
            WorkspaceIndicator { id: workspaceIndicator; panel: taskbar.panel }
            BindingMode { panel: taskbar.panel }
            WallpapersButton { id: wallpapersButton; panel: taskbar.panel }
            ProfilesButton { id: profilesButton; panel: taskbar.panel }
            TilingButton { panel: taskbar.panel }
            // The system tray: the status icons of applications, in the order they appeared.
            // Passive ones stay hidden, and so does the tray when none is left.
            Row {
                id: tray
                objectName: "tray"
                visible: shell.widgets.tray && shell.tray.shown > 0
                Layout.alignment: Qt.AlignVCenter
                Repeater {
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
                Layout.preferredWidth: Theme.spacingL + Theme.spacingXS; Layout.fillHeight: true
                onClicked: { taskbar.panel.closeMenus(); shell.tasks.showDesktop() }
                Accessible.name: "Show desktop"
                BarTip { panel: taskbar.panel; owner: showDesktopButton; text: "Show desktop" }
                background: Rectangle {
                    anchors.right: parent.right
                    width: 3
                    color: showDesktopButton.pressed ? Theme.accentHover : showDesktopButton.hovered ? Theme.accent : Theme.border
                    Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                }
            }
        }
        Rectangle {
            visible: shell.error.length > 0
            anchors.fill: parent; anchors.margins: Theme.spacingS
            color: Theme.dangerSurface; radius: Theme.radiusSmall
            Text { anchors.left: parent.left; anchors.right: dismiss.left; anchors.verticalCenter: parent.verticalCenter; anchors.margins: Theme.spacingL; text: shell.error; color: Theme.text; elide: Text.ElideRight; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
            CloseButton {
                id: dismiss
                anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
                anchors.verticalCenter: parent.verticalCenter
                Accessible.name: "Dismiss"
                onClicked: shell.clearError()
            }
        }
    }
}
