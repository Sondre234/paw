// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw

// The keyboard on the bar, as Windows' Win+T has it: the taskbar_focus action asks the panel on
// the focused monitor for it (`taskbar OUTPUT`), and the popover takes the keyboard for this item,
// which walks the bar's buttons with it. The stops are the buttons of the taskbar's row, and every
// icon on the dock; the one with the window that had the
// keyboard is selected first, else the first. Left and Right, Home and End move between them, and
// the windows of the one selected show at once, as resting the pointer on it shows them, the card
// gliding along. Up (Down from a bar along the top) goes into the card of pictures or the list,
// where Left and Right (Up and Down in the list) move between the windows, a picture selected
// for a moment peeking at its window as one the pointer rests on does, and Down (Up) comes back.
// Enter or Space does what a click does and gives the keyboard back; the window that had it
// counts as the focused one, which the bar no longer shows as focused while the keyboard is here.
// Delete closes the selected window, as its cross does. The Menu key or Shift+F10 opens the
// menu of the button or the window, and closing the menu comes back here. Escape, or the action
// again, gives the keyboard back to the window that had it, changing nothing. The pointer moving
// over the bar or the card, or a press anywhere, hands the bar back to the pointer (handOver).
Item {
    id: keys
    required property var panel
    // Whether the bar has the keyboard.
    property bool active: false
    // The selected stop, and its place among the stops, kept for when it goes.
    property Item button: null
    property int index: -1
    // The selected window on the card or in the list, by its place there; -1 on the buttons.
    property int window: -1
    // The window that had the keyboard as the bar took it, -1 for none.
    property int from: -1
    // Whether the menu open now was opened from here, which its closing comes back to.
    property bool menuOpened: false
    // What a screen reader says: the selected button's or window's own name.
    readonly property Item described: window >= 0 && panel.groupPopup && panel.groupWindows.count > 0
                                      ? panel.groupPopup.windowAt(window) : button
    Accessible.role: window >= 0 ? Accessible.ListItem : Accessible.Button
    Accessible.name: described ? described.Accessible.name : ""

    // The stops in the bar's order: the dock's items, or the numbers of the task list's rows, as
    // it makes the buttons outside its view only as they come into it.
    function stops() {
        var list = [], i
        var taskbar = panel.taskbar, dock = panel.dock
        if (taskbar) {
            for (i = 0; i < taskbar.tasks.count; ++i)
                list.push(i)
        } else if (dock) {
            var icons = [dock.launcherButton]
            for (i = 0; i < dock.pins.count; ++i)
                icons.push(dock.pins.itemAt(i))
            for (i = 0; i < dock.running.count; ++i)
                icons.push(dock.running.itemAt(i))
            icons.push(dock.trash)
            for (i = 0; i < icons.length; ++i)
                if (icons[i] && icons[i].visible)
                    list.push(icons[i])
        }
        return list
    }
    // A stop's item: a row of the task list is scrolled into view, where it is made if it was not.
    function itemOf(stop) {
        if (typeof stop !== "number")
            return stop
        var tasks = panel.taskbar ? panel.taskbar.tasks : null
        if (!tasks)
            return null
        tasks.positionViewAtIndex(stop, ListView.Contain)
        return tasks.itemAtIndex(stop)
    }
    // The task list's item for a stop as it is, without scrolling; null for none.
    function shownItem(stop) {
        if (typeof stop !== "number")
            return stop
        return panel.taskbar ? panel.taskbar.tasks.itemAtIndex(stop) : null
    }
    // Where the selected button is among the stops, -1 when it is not.
    function place(list) {
        for (var i = 0; i < list.length; ++i)
            if (button && shownItem(list[i]) === button)
                return i
        return -1
    }
    // The stop the pointer is on, null for none.
    function hoveredStop() {
        var list = stops()
        for (var i = 0; i < list.length; ++i) {
            var item = shownItem(list[i])
            if (item && item.hovered)
                return item
        }
        return null
    }
    // The focused window as the bar shows it, -1 for none.
    function focused() {
        return keys.panel.allWindows.activeTask
    }
    // The stop with window `id` among its windows, -1 for none.
    function stopOf(list, id) {
        // A row stands for its application's windows when they are grouped.
        var row = panel.taskbar ? panel.taskbar.tasks.model.rowOf(id) : -1
        for (var i = 0; i < list.length; ++i) {
            var stop = list[i]
            if (typeof stop === "number") {
                if (stop === row)
                    return i
            } else if (typeof stop.dragWindows === "function" &&
                       stop.dragWindows().some(function(window) { return window.taskId === id })) {
                return i
            }
        }
        return -1
    }

    function toggle() {
        if (active) {
            leave()
            return
        }
        var list = stops()
        if (list.length === 0)
            return
        from = focused()
        panel.closeMenus()
        pointer = null
        active = true
        select(list, Math.max(0, stopOf(list, from)))
        forceActiveFocus()
    }
    // Gives the keyboard back, to the window that had it unless something else took it.
    function leave() {
        if (!active)
            return
        forget()
        panel.closeGroup()
    }
    // The pointer takes over from the keyboard: what it is on goes on as on hover.
    function handOver() {
        if (!active)
            return
        forget()
        panel.pointerTakesOver(hoveredStop())
    }
    function forget() {
        active = false
        window = -1
        button = null
        index = -1
        from = -1
        menuOpened = false
    }

    function select(list, at) {
        if (list.length === 0) {
            leave()
            return
        }
        index = Math.max(0, Math.min(list.length - 1, at))
        window = -1
        button = itemOf(list[index])
        showWindows()
    }
    function move(step) {
        var list = stops(), at = place(list)
        select(list, (at < 0 ? index : at) + step)
    }
    // The selected button's windows show at once, as resting the pointer on it shows them; the
    // card glides over from the last button's.
    function showWindows() {
        var item = button
        if (!active || panel.menuOpen)
            return
        panel.stopWaiting()
        if (item && typeof item.dragWindows === "function" && item.dragWindows().length > 0 &&
            panel.showsWindows(item)) {
            panel.groupPending = item
            panel.openGroup(item)
        } else {
            panel.groupOpen = false
        }
    }
    // The stops changed: the selected one stays, or one gone (its window closed) leaves its place
    // to the next.
    function sync() {
        if (!active)
            return
        var list = stops()
        if (list.length === 0) {
            leave()
            return
        }
        var at = place(list)
        if (at < 0 || !button.enabled)
            select(list, index)
        else
            index = at
    }
    // Fewer windows on the card: the last one is selected in place of one gone.
    function fitWindow() {
        var count = panel.groupOpen ? panel.groupWindows.count : 0
        if (window >= count)
            window = count - 1
    }
    onButtonChanged: if (active && !button) Qt.callLater(sync)
    Connections {
        target: keys.panel.taskSource
        ignoreUnknownSignals: true
        function onRowsInserted() { Qt.callLater(keys.sync) }
        function onRowsRemoved() { Qt.callLater(keys.sync) }
        function onModelReset() { Qt.callLater(keys.sync) }
    }
    Connections {
        target: keys.panel.groupWindows
        function onCountChanged() { keys.fitWindow() }
    }

    // What a click does: a window's button brings it up, or minimizes it when it had the
    // keyboard; a stack's brings up the window after that one, or its first. A pinned
    // application's button, and the dock's icons, are clicked.
    function activate() {
        var item = button
        if (!item)
            return
        var windows = typeof item.dragWindows === "function" ? item.dragWindows() : []
        if (windows.length === 0 || panel.dock) {
            item.clicked()
            return
        }
        var at = -1
        for (var i = 0; i < windows.length; ++i)
            if (windows[i].taskId === from)
                at = i
        bringUp(windows[(at + 1) % windows.length])
    }
    // A window as the task source's rows have it ({taskId, minimized, ...}).
    function bringUp(row) {
        var id = row.taskId, minimize = id === from && !row.minimized
        var source = panel.taskSource
        leave()
        if (minimize)
            source.minimize(id)
        else
            source.activate(id)
    }
    function selectedWindow() {
        return window >= 0 && panel.groupOpen ? panel.groupWindows.windows[window] || null : null
    }
    function openMenu() {
        var item = window >= 0 ? (panel.groupPopup ? panel.groupPopup.windowAt(window) : null) : button
        if (!item || typeof item.keyMenu !== "function")
            return
        menuOpened = true
        item.keyMenu()
        if (!panel.menuOpen)
            menuOpened = false
    }
    // A menu opened from here closed: the keyboard comes back, and so do the windows shown.
    function resume() {
        if (!menuOpened)
            return
        menuOpened = false
        if (!active)
            return
        forceActiveFocus()
        sync()
        showWindows()
    }

    // Where the pointer last was over the bar or the card, in the popover's coordinates; null
    // while unknown since the bar took the keyboard. Only its moving hands the bar back to it, not
    // the bar or the card coming under it, which tells where it is again.
    property var pointer: null
    function pointerAt(x, y) {
        if (!active)
            return
        if (pointer && (Math.abs(x - pointer.x) > 2 || Math.abs(y - pointer.y) > 2))
            handOver()
        else
            pointer = Qt.point(x, y)
    }

    Keys.onPressed: (event) => { event.accepted = keys.active && keys.key(event) }
    function key(event) {
        // Away from the bar, toward the card, and back.
        var away = panel.dockSide === Qt.TopEdge ? Qt.Key_Up : Qt.Key_Down
        var toward = away === Qt.Key_Up ? Qt.Key_Down : Qt.Key_Up
        var count = panel.groupOpen ? panel.groupWindows.count : 0
        var enter = event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space
        if (event.key === Qt.Key_Escape) {
            leave()
        } else if (event.key === Qt.Key_Menu || event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier)) {
            openMenu()
        } else if (window < 0) {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                move(event.key === Qt.Key_Left ? -1 : 1)
            else if (event.key === Qt.Key_Home)
                select(stops(), 0)
            else if (event.key === Qt.Key_End)
                select(stops(), stops().length - 1)
            else if (enter)
                activate()
            else if (event.key === away && count > 0)
                // The list's row nearest the bar, or the card's first picture.
                window = panel.groupListOpen && away === Qt.Key_Up ? count - 1 : 0
            else
                return false
        } else if (enter) {
            if (selectedWindow())
                bringUp(selectedWindow())
        } else if (event.key === Qt.Key_Delete) {
            if (selectedWindow())
                panel.taskSource.close(selectedWindow().taskId)
        } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
            window = event.key === Qt.Key_Home ? 0 : count - 1
        } else if (!panel.groupListOpen) {
            // The pictures side by side.
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                window = Math.max(0, Math.min(count - 1, window + (event.key === Qt.Key_Left ? -1 : 1)))
            else if (event.key === toward)
                window = -1
            else
                return event.key === away
        } else {
            // The rows, one above the other: from the one nearest the bar, back to its button.
            var step = away === Qt.Key_Up ? 1 : -1, nearest = away === Qt.Key_Up ? count - 1 : 0
            if (event.key === toward)
                window = window === nearest ? -1 : window + step
            else if (event.key === away)
                window = Math.max(0, Math.min(count - 1, window - step))
            else
                return false
        }
        return true
    }
}
