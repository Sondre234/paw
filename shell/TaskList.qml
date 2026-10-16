// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import Paw

// The taskbar's buttons in one row (TaskbarModel): the launchers of the pinned applications with
// no window, and every window, pinned or not, in the order they were dragged into. A pinned
// application's first window opens in its launcher's place, any other at the end, and a window's
// button follows its monitor among its application's (TaskbarOrder). Grouped
// (shell.groupWindows), an application's windows share a button, a pinned one's in its place.
// Along a side the row is a column, of icons alone.
ListView {
    id: taskList
    required property var panel
    objectName: "taskList"
    readonly property bool vertical: panel.vertical
    // As tall as a button (as wide, down a side): a horizontal list places each delegate at its
    // top, whatever the delegate's own y.
    Layout.fillWidth: !vertical; Layout.preferredHeight: vertical ? -1 : Theme.barButtonHeight
    Layout.fillHeight: vertical; Layout.preferredWidth: vertical ? panel.barItemWidth : -1
    orientation: vertical ? ListView.Vertical : ListView.Horizontal; spacing: Theme.spacingS; clip: true
    // Dragging moves a single button, not the list; the wheel scrolls an overflowing one.
    interactive: false
    // The button being dragged, the button whose place it takes, and how far the buttons
    // in between step aside.
    property int dragFrom: -1
    property int dragTo: -1
    property real dragStep: 0
    model: TaskbarModel { controller: shell; sourceModel: taskList.panel.taskSource; grouped: shell.groupWindows }
    // A launcher is as wide as its icon; the windows' buttons share what room is left.
    readonly property real windowWidth: Math.min(185, Math.max(92, (width - model.launchers * (Theme.barButtonWidth + spacing)) /
                                                                   Math.max(1, count - model.launchers) - 4))
    // A button fades and grows in as its window opens or its application is pinned, drawing its
    // activity line out, and shrinks away as it goes, taking no more clicks; the buttons beside it
    // slide over to make room or close the gap. Dragged ones slide aside quicker, keeping up with
    // the pointer.
    add: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { property: "scale"; from: Theme.growFrom; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { property: "reveal"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    remove: Transition {
        PropertyAction { property: "enabled"; value: false }
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationFast; easing.type: Theme.easingExit }
        NumberAnimation { property: "scale"; to: Theme.growFrom; duration: Theme.durationFast; easing.type: Theme.easingExit }
    }
    // A button displaced while it was still coming in ends whole.
    displaced: Transition {
        NumberAnimation { property: taskList.vertical ? "y" : "x"; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { properties: "opacity,scale,reveal"; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    moveDisplaced: Transition {
        NumberAnimation { property: taskList.vertical ? "y" : "x"; duration: Theme.durationFast; easing.type: Theme.easing }
    }
    WheelHandler {
        // Qt takes the whole pointer for a touchpad once the compositor offers gestures
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        enabled: taskList.vertical ? taskList.contentHeight > taskList.height : taskList.contentWidth > taskList.width
        onWheel: (event) => {
            var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
            if (taskList.vertical)
                taskList.contentY = Math.max(0, Math.min(taskList.contentHeight - taskList.height,
                                                         taskList.contentY - delta / 2))
            else
                taskList.contentX = Math.max(0, Math.min(taskList.contentWidth - taskList.width,
                                                         taskList.contentX - delta / 2))
        }
    }
    delegate: TaskButton {
        id: taskButton
        required property int index
        panel: taskList.panel
        objectName: launcher ? "pinned:" + app.appId : pinned ? "pinnedTask:" + app.appId : ""
        // Grouped, the windows it stands for: its pinned application's, or those with its app id;
        // windows without one have nothing to group by. The filter reads them only then.
        readonly property bool grouping: shell.groupWindows && !launcher && (pinned || appId !== "")
        property TaskFilter appWindows: TaskFilter {
            controller: shell; sourceModel: taskButton.grouping ? taskList.panel.taskSource : null
            app: taskButton.pinned ? taskButton.app.appId : ""
            windowApp: taskButton.pinned ? "" : taskButton.appId
        }
        group: grouping ? appWindows : null
        groupSlot: pinned ? app.appId : ""
        groupWindowApp: pinned ? "" : appId
        width: taskList.vertical ? panel.barItemWidth : panel.iconsOnly || launcher ? Theme.barButtonWidth : taskList.windowWidth
        z: reorder.active ? 1 : 0
        readonly property real shift: {
            var from = taskList.dragFrom, to = taskList.dragTo
            if (from < 0 || index === from)
                return 0
            if (from < to && index > from && index <= to)
                return -taskList.dragStep
            if (from > to && index >= to && index < from)
                return taskList.dragStep
            return 0
        }
        property real shiftX: shift
        Behavior on shiftX { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        // Along the list: across, or down a side. Over a workspace it settles back into its place,
        // what it carries floating on at the pointer.
        property real lift: reorder.active && taskList.panel.dropWorkspace > 0 ? 1 : 0
        Behavior on lift { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        readonly property real moved: reorder.active ? (1 - lift) * (taskList.vertical ? reorder.activeTranslation.y : reorder.activeTranslation.x)
                                                     : taskButton.shiftX
        transform: Translate { x: taskList.vertical ? 0 : taskButton.moved; y: taskList.vertical ? taskButton.moved : 0 }
        // The button follows the pointer through the list, the buttons whose middles it passes
        // making way, and moves on release, on every taskbar; the press never becomes a click.
        // Only a launcher's drag moves its pin. Over a workspace of the indicator a window's
        // button moves nothing in the list: let go there, its windows go to that workspace (the
        // panel's moveButtonDrag and endButtonDrag).
        DragHandler {
            id: reorder
            target: null
            xAxis.enabled: !taskList.vertical
            yAxis.enabled: taskList.vertical
            onActiveTranslationChanged: {
                if (!active)
                    return
                if (!taskButton.launcher) {
                    const at = taskList.panel.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                    if (taskList.panel.moveButtonDrag(taskButton.iconName, at.x, at.y) > 0) {
                        taskList.dragFrom = taskList.dragTo = taskButton.index
                        return
                    }
                }
                // A button's start and length along the list.
                function start(item) { return taskList.vertical ? item.y : item.x }
                function length(item) { return taskList.vertical ? item.height : item.width }
                var along = taskList.vertical ? activeTranslation.y : activeTranslation.x
                var centre = start(taskButton) + length(taskButton) / 2 + along, to = taskButton.index
                for (var i = 0; i < taskList.count; ++i) {
                    var other = taskList.itemAtIndex(i)
                    if (other && ((i > taskButton.index && centre >= start(other) + length(other) / 2) ||
                                  (i < taskButton.index && centre <= start(other) + length(other) / 2 && to === taskButton.index)))
                        to = i
                }
                taskList.dragStep = length(taskButton) + taskList.spacing
                taskList.dragTo = to
                taskList.dragFrom = taskButton.index
            }
            // Cancelled rather than let go, it drops nothing on a workspace.
            onCanceled: taskList.panel.cancelButtonDrag()
            onActiveChanged: {
                if (active)
                    return
                var from = taskList.dragFrom, to = taskList.dragTo
                taskList.dragFrom = taskList.dragTo = -1
                if (!taskButton.launcher && taskList.panel.dropWorkspace > 0) {
                    taskList.panel.releaseButtonDrag(taskButton.dragWindows())
                    return
                }
                if (from >= 0 && to >= 0 && to !== from)
                    taskList.model.move(from, to)
            }
        }
        // Where the windows it stands for are, for the compositor to minimize them into and
        // restore them out of: the whole button, as the list scrolls, slides or drags it and
        // wherever the bar is; nowhere while scrolled out of the list's sight, or while the
        // tiling bar stands in for the taskbar.
        readonly property var placedTasks: launcher ? []
            : group && group.count > 0 ? group.windows.map(function(w) { return w.taskId }) : [taskId]
        readonly property rect placedRect: {
            void [x, y, width, height, moved, taskList.contentX, taskList.contentY, taskList.x, taskList.y,
                  taskList.width, taskList.height, panel.bar.x, panel.bar.y, panel.surfaceLeft, panel.surfaceTop]
            if (!parent || !taskList.visible)
                return Qt.rect(0, 0, 0, 0)
            var at = parent.mapToItem(taskList, x + (taskList.vertical ? 0 : moved), y + (taskList.vertical ? moved : 0))
            if (at.x + width <= 0 || at.y + height <= 0 || at.x >= taskList.width || at.y >= taskList.height)
                return Qt.rect(0, 0, 0, 0)
            var point = taskList.mapToItem(panel, at.x, at.y)
            return Qt.rect(panel.surfaceLeft + point.x, panel.surfaceTop + point.y, width, height)
        }
        function placeWindows() { panel.placeWindows(taskButton, placedTasks, placedRect, 0) }
        onPlacedRectChanged: placeWindows()
        onPlacedTasksChanged: placeWindows()
        Component.onCompleted: placeWindows()
        // Going while it is dragged (its window closing), it takes back what the drag lit and moved
        // aside, which its handler, going too, never says has ended.
        Component.onDestruction: {
            if (!reorder.active)
                return
            taskList.dragFrom = taskList.dragTo = -1
            taskList.panel.cancelButtonDrag()
        }
        // The button, whose press the drag takes over, still hears the drag's moves, each of which
        // would press it again and leave it pressed past the drop: while dragged, it holds no point.
        containmentMask: reorder.active ? holdsNone : null
        QtObject {
            id: holdsNone
            function contains(point: point): bool { return false }
        }
    }
}
