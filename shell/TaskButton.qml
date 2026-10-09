// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Paw

// A button on the taskbar (a row of TaskbarModel): a window's, its icon with its title beside it
// unless shell.iconsOnly, else as a tooltip; or a pinned application's launcher (`app`, taskId -1),
// its icon alone, its name as a tooltip, which starts it.
// With shell.groupWindows the button stands for all its application's windows, `group`, a pinned
// one's showing its launcher's icon: stacked when there are several, it cycles through them and
// lists them on hover. With shell.thumbnails, resting on it shows pictures of its windows instead,
// with their titles, and it has no tooltip.
Button {
    id: task
    required property int taskId
    required property string title
    required property string appId
    required property bool active
    required property bool minimized
    required property bool urgent
    // The pinned application it stands for, as shell.pinned has it; {} for a window's button.
    required property var app
    readonly property bool pinned: !!app && app.appId !== undefined
    readonly property bool launcher: taskId < 0
    // The panel it is on, whose menus and group list it opens.
    required property Item panel
    // The icon of the pinned application, else of the application the window belongs to, else
    // one guessed from its app ID.
    property string iconName: pinned ? app.icon : shell.iconFor(appId)
    property TaskFilter group: null
    // Where the hover list finds the group's windows (see the panel's groupSlot).
    property string groupSlot: ""
    property string groupWindowApp: appId
    readonly property int windows: group ? group.count : 1
    readonly property bool stacked: windows > 1
    readonly property bool shownActive: group && group.count > 0 ? group.activeTask >= 0 : active
    readonly property bool shownMinimized: group && group.count > 0 ? group.minimized : minimized
    // Asking for attention: any window of a stack will do.
    readonly property bool shownUrgent: group && group.count > 0 ? group.urgent : urgent
    // The windows a drag resting on it brings forward (the panel's dragDelay), as
    // {taskId, active, minimized} each; it is lit while the drag is over it, as under the pointer.
    function dragWindows() {
        if (launcher)
            return []
        return group && group.count > 0 ? group.windows : [{ taskId: taskId, active: active, minimized: minimized }]
    }
    readonly property bool dragOver: panel.dragButton === task
    // The keyboard on the bar is at it (the panel's barKeys), which rings it; the Menu key there
    // opens its menu.
    readonly property bool keySelected: panel.barKeys.button === task && panel.barKeys.window < 0
    function keyMenu() { openMenu() }
    function openMenu() {
        if (launcher) panel.openContextMenu(task, 0, -1, app); else panel.openContextMenu(task, 0, taskId, appId)
    }
    height: Theme.barButtonHeight
    // Hovering a stacked button lists its windows, whatever the platform thinks of hover.
    hoverEnabled: true
    // The icon sits above the activity line, fitting however short the bar is.
    topPadding: Theme.spacingXS; bottomPadding: Theme.spacingS + Theme.spacingXS
    leftPadding: Theme.spacingS + Theme.spacingXS; rightPadding: leftPadding
    onClicked: {
        if (launcher) {
            if (shell.launch(app.appId)) task.panel.closeMenus()
            return
        }
        task.panel.closeMenus()
        shell.tasks.activate(stacked ? group.nextTask() : taskId)
    }
    // A launcher has no windows to show.
    onHoveredChanged: if (!launcher || !hovered) task.panel.hoverGroup(task, hovered)
    onPressedChanged: if (pressed) task.panel.closeGroup()
    Accessible.name: launcher ? title : (stacked ? title + " and " + (windows - 1) + " more" : title) + (shownUrgent ? " (needs attention)" : "")
    // The panel's surface is only as tall as the bar, so an in-window tooltip would be
    // squeezed onto the icon and swallow its clicks; a popup window of its own sits above
    // the bar instead.
    BarTip {
        panel: task.panel; owner: task
        visible: task.launcher ? (task.hovered || task.keySelected) && !task.pressed && text.length > 0 && !task.panel.menuOpen
                               : shell.iconsOnly && !task.stacked && !task.panel.thumbnails && !task.pressed &&
                                 (task.hovered && !task.panel.expanded || task.keySelected && !task.panel.groupOpen && !task.panel.menuOpen)
        text: task.title
    }
    // How far the activity line has come in, from 0 to 1, drawn out from its middle as the
    // button arrives.
    property real reveal: 1
    // A button arriving where there was none (its window opening) fades and grows in, drawing its
    // line out, which the task list does with its own transition; a launcher whose application's
    // window opens, its icon there already, only draws its line out.
    ParallelAnimation {
        id: entering
        NumberAnimation { target: task; property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { target: task; property: "scale"; from: Theme.growFrom; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    NumberAnimation {
        id: revealing
        target: task; property: "reveal"
        from: 0; to: 1
        duration: Theme.durationNormal; easing.type: Theme.easing
    }
    function enter() { entering.restart(); revealing.restart() }
    function appear() { revealing.restart() }
    onLauncherChanged: if (!launcher) appear()
    background: ButtonFill {
        hovered: task.hovered || task.dragOver
        pressed: task.pressed
        active: task.shownActive
        color: task.shownUrgent ? Theme.urgentSubtle : stateColor
        border.width: task.shownUrgent ? 1 : 0; border.color: Theme.urgent
        // Several windows: a second button's edge peeks out behind this one.
        Rectangle {
            visible: task.stacked
            z: -1; x: 3; y: 2; width: parent.width; height: parent.height - 4; radius: Theme.radiusSmall
            color: "transparent"; border.width: 1
            border.color: task.shownActive ? Theme.alpha(Theme.text, 0.3) : Theme.border
        }
        // On its edge, which the task list clips at, under the activity line.
        FocusRing {
            objectName: "taskFocusRing"
            visible: task.keySelected
            radius: parent.radius
        }
        // The activity line along the bottom: long under the focused window's button, short
        // under the others, split in two for a stack; in the accent colour, the urgent one, or
        // dimmed while minimized. Its length and colour ease to each new state.
        Item {
            id: line
            objectName: "taskLine"
            visible: !task.launcher
            readonly property real segment: task.shownActive ? (shell.iconsOnly ? 18 : 28) / (task.stacked ? 2 : 1)
                                                             : (task.stacked ? 6 : 10)
            property real first: segment
            property real second: task.stacked ? segment : 0
            property real gap: task.stacked ? 3 : 0
            property color tint: task.shownUrgent ? Theme.urgent : task.shownMinimized ? Theme.textDisabled : Theme.accent
            Behavior on first { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on second { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on gap { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            Behavior on tint { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
            anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
            // Drawn out from the middle by `reveal`, past the easing of its length.
            width: (first + gap + second) * task.reveal; height: 3
            Rectangle { width: line.first * task.reveal; height: parent.height; radius: 1; color: line.tint }
            Rectangle {
                visible: width > 0
                x: (line.first + line.gap) * task.reveal; width: line.second * task.reveal
                height: parent.height; radius: 1
                color: line.tint
            }
        }
        // A dot that pulses a few times when the window asks for attention, then holds.
        Rectangle {
            objectName: "taskUrgent"
            visible: task.shownUrgent
            anchors.left: parent.left; anchors.top: parent.top; anchors.margins: Theme.spacingXS
            width: Theme.spacingM; height: width; radius: width / 2
            color: Theme.urgent
            SequentialAnimation on opacity {
                running: task.shownUrgent
                loops: 6; alwaysRunToEnd: true
                NumberAnimation { to: 0.3; duration: Theme.duration(450) }
                NumberAnimation { to: 1; duration: Theme.duration(450) }
            }
        }
        // How many windows a stack has, on its corner.
        Badge {
            objectName: "taskCount"
            anchors.right: parent.right; anchors.top: parent.top
            count: task.stacked ? task.windows : 0
        }
    }
    contentItem: RowLayout {
        spacing: Theme.spacingS + Theme.spacingXS
        Item { Layout.fillWidth: shell.iconsOnly || task.launcher }
        BarAppIcon {
            name: task.iconName
            pressed: task.pressed
            size: Math.min(Theme.appIconSize, task.availableHeight)
            Layout.preferredWidth: size; Layout.preferredHeight: size
        }
        Text { visible: !shell.iconsOnly && !task.launcher; text: task.title; textFormat: Text.PlainText; color: Theme.text; elide: Text.ElideRight; Layout.fillWidth: true; font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily }
        Item { Layout.fillWidth: shell.iconsOnly || task.launcher }
    }
    // Right-click opens the button's menu; middle-click closes a window.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                task.openMenu()
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton && !task.launcher) { task.panel.closeMenus(); shell.tasks.close(task.taskId) }
        }
    }
}
