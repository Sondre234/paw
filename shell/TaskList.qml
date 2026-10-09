// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import Paw

// The taskbar's buttons in one row (TaskbarModel): the launchers of the pinned applications with
// no window, and every window, pinned or not, in the order they were dragged into. A pinned
// application's first window opens in its launcher's place, any other at the end. Grouped
// (shell.groupWindows), an application's windows share a button, a pinned one's in its place.
ListView {
    id: taskList
    required property var panel
    objectName: "taskList"
    // As tall as a button: a horizontal list places each delegate at its top, whatever the
    // delegate's own y.
    Layout.fillWidth: true; Layout.preferredHeight: Theme.barButtonHeight
    orientation: ListView.Horizontal; spacing: Theme.spacingS; clip: true
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
        NumberAnimation { property: "x"; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { properties: "opacity,scale,reveal"; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
    }
    moveDisplaced: Transition { NumberAnimation { property: "x"; duration: Theme.durationFast; easing.type: Theme.easing } }
    WheelHandler {
        // Qt takes the whole pointer for a touchpad once the compositor offers gestures
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        enabled: taskList.contentWidth > taskList.width
        onWheel: (event) => {
            var delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
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
        // windows without one have nothing to group by.
        property TaskFilter appWindows: TaskFilter {
            controller: shell; sourceModel: taskList.panel.taskSource
            app: taskButton.pinned ? taskButton.app.appId : ""
            windowApp: taskButton.pinned ? "" : taskButton.appId
        }
        group: shell.groupWindows && !launcher && (pinned || appId !== "") ? appWindows : null
        groupSlot: pinned ? app.appId : ""
        groupWindowApp: pinned ? "" : appId
        width: shell.iconsOnly || launcher ? Theme.barButtonWidth : taskList.windowWidth
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
        transform: Translate { x: reorder.active ? reorder.activeTranslation.x : taskButton.shiftX }
        // The button follows the pointer through the list, the buttons whose middles it passes
        // making way, and moves on release, on every taskbar; the press never becomes a click.
        // Only a launcher's drag moves its pin.
        DragHandler {
            id: reorder
            target: null
            yAxis.enabled: false
            onActiveTranslationChanged: {
                if (!active)
                    return
                var centre = taskButton.x + taskButton.width / 2 + activeTranslation.x, to = taskButton.index
                for (var i = 0; i < taskList.count; ++i) {
                    var other = taskList.itemAtIndex(i)
                    if (other && ((i > taskButton.index && centre >= other.x + other.width / 2) ||
                                  (i < taskButton.index && centre <= other.x + other.width / 2 && to === taskButton.index)))
                        to = i
                }
                taskList.dragStep = taskButton.width + taskList.spacing
                taskList.dragTo = to
                taskList.dragFrom = taskButton.index
            }
            onActiveChanged: {
                if (active)
                    return
                var from = taskList.dragFrom, to = taskList.dragTo
                taskList.dragFrom = taskList.dragTo = -1
                if (from >= 0 && to >= 0 && to !== from)
                    taskList.model.move(from, to)
            }
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
