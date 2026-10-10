// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw
import "WindowMenu.js" as WindowMenu

// The context menu of a window's button (its pinned application's too), of a pinned
// application's button, or of the bar itself, whose appearance entry opens the profiles beside
// it. It opens where the bar was pressed. A window's begins with its application's icon and name
// over its title, then offers what the application starts, what can be done to the window (by its
// state, and where it is), pinning and closing it; a stacked button's acts on all its windows. A
// pinned application's offers what it starts, and unpinning it. On the dock of the macOS style it
// opens above the icon pressed, and an application's is macOS's: its windows to bring up, what it
// starts, keeping it in the dock, hiding and quitting it. Each ends with killing the processes
// of its windows.
PopupMenu {
    id: contextMenu
    required property var panel
    parent: panel.popupLayer
    objectName: "contextMenu"
    entryName: "contextMenuItem"
    open: panel.taskMenuId >= 0 || panel.pinMenuApp !== null || panel.barMenuOpen
    anchorRect: panel.dockAnchor(panel.contextMenuX, 0)
    side: panel.dockSide
    alignment: panel.macos ? Qt.AlignHCenter : Qt.AlignLeft
    bounds: panel.popupArea
    // Opened by the keyboard on the bar (the Menu key), its first entry is highlighted.
    initialIndex: panel.barKeys.menuOpened ? 0 : -1
    // Closing only this menu: an entry may have opened the launcher.
    onDismissed: { panel.taskMenuId = -1; panel.pinMenuApp = null; panel.barMenuOpen = false }

    // The windows a task menu is about, as data from the panel's windows, which follows their
    // changes while the menu is open: the one right-clicked, or those of a stacked button, found
    // where its hover list finds them.
    TaskFilter {
        id: menuWindows
        readonly property var group: contextMenu.panel.taskMenuGroup
        controller: shell
        sourceModel: contextMenu.panel.taskMenuId >= 0 ? contextMenu.panel.taskSource : null
        app: group ? group.slot : ""
        windowApp: group ? group.windowApp : ""
        taskId: group ? -1 : contextMenu.panel.taskMenuId
    }
    // The menu's title: the application's icon and name (the window's app id when it has no
    // entry), with `line` under it; without either, `line` alone.
    function titleEntry(record, windowAppId, line) {
        var named = record !== null || windowAppId !== ""
        return { title: record ? record.name : (windowAppId || line),
                 icon: record ? record.icon : shell.iconFor(windowAppId),
                 secondary: named ? line : "", objectName: "contextMenuTitle" }
    }
    // What the application offers to start besides itself (its desktop actions, with their
    // icons), then `open` starting it, unless an action of its own is a new window already.
    function launchEntries(record, open) {
        if (!record)
            return []
        var actions = shell.appActions(record.appId)
        var entries = actions.map(function(action) {
            return { text: action.name, icon: action.icon, objectName: "contextMenuAction",
                     run: function() { shell.launchAction(record.appId, action.action) } }
        })
        var newWindow = actions.some(function(action) {
            return action.action === "new-window" || action.name.toLowerCase() === "new window"
        })
        return open.toLowerCase() === "new window" && newWindow ? entries
            : entries.concat([{ text: open, icon: open === "Open" ? "app-window" : "plus",
                                run: function() { shell.launch(record.appId) } }])
    }
    // The menu of window `task`, or of a stacked button, which acts on all its windows: its
    // title counts them, and it minimizes or restores, moves and closes them together.
    function taskEntries(task, tasks) {
        var windows = menuWindows.windows
        var lead = windows.filter(function(w) { return w.taskId === task })[0] || windows[0] ||
                   { taskId: task, appId: "", title: "" }
        var record = panel.appRecord(lead.appId)
        var stacked = windows.length > 1
        var close = stacked
            ? { text: "Close all " + windows.length + " windows", icon: "x", danger: true,
                objectName: "contextMenuClose",
                run: function() { windows.forEach(function(w) { tasks.close(w.taskId) }) } }
            : { text: "Close window", icon: "x", danger: true, objectName: "contextMenuClose",
                run: function() { tasks.close(lead.taskId) } }
        return sections([[titleEntry(record, lead.appId, stacked ? windows.length + " windows" : lead.title)],
                         launchEntries(record, "New window"),
                         stacked ? stackEntries(windows, tasks) : windowEntries(lead, tasks),
                         (panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : []).concat([close])
                             .concat(killEntries(windows, tasks))])
    }
    // Killing the processes that made `windows` with SIGKILL, for an application that does not
    // close, one window of each standing for it; none where no process is known. On the dock it
    // is macOS's Force Quit, without an icon.
    function killEntries(windows, tasks) {
        var byPid = {}
        windows.forEach(function(w) { if (w.pid > 0 && !(w.pid in byPid)) byPid[w.pid] = w.taskId })
        var ids = Object.keys(byPid).map(function(pid) { return byPid[pid] })
        if (ids.length === 0)
            return []
        var kill = function() { ids.forEach(function(id) { tasks.kill(id) }) }
        return [panel.macos
                ? { text: "Force Quit", objectName: "contextMenuKill", run: kill }
                : { text: ids.length > 1 ? "Kill " + ids.length + " processes" : "Kill process",
                    icon: "octagon-x", danger: true, objectName: "contextMenuKill", run: kill }]
    }
    // An application's menu on the dock: its windows, the focused one marked, then what it starts,
    // keeping it there, and hiding or quitting it, which minimizes or closes every window.
    function dockEntries(task, tasks) {
        var windows = menuWindows.windows
        var lead = windows.filter(function(w) { return w.taskId === task })[0] || windows[0] ||
                   { taskId: task, appId: "", title: "" }
        var record = panel.appRecord(lead.appId)
        var listed = windows.map(function(w) {
            return { text: w.title || (record ? record.name : w.appId), toggle: "check", checked: w.active === true,
                     objectName: "contextMenuWindow", run: function() { tasks.activate(w.taskId) } }
        })
        return sections([listed, bare(launchEntries(record, "New Window")),
                         panel.taskMenuApp ? [panel.pinAction(panel.taskMenuApp)] : [],
                         [{ text: "Hide", objectName: "contextMenuHide",
                            run: function() { windows.forEach(function(w) { if (!w.minimized) tasks.minimize(w.taskId) }) } },
                          { text: "Quit", objectName: "contextMenuClose",
                            run: function() { windows.forEach(function(w) { tasks.close(w.taskId) }) } }]
                         .concat(killEntries(windows, tasks))])
    }
    // Entries without their icons, as macOS's menus have them.
    function bare(entries) {
        return entries.map(function(entry) { return { text: entry.text, objectName: entry.objectName, run: entry.run } })
    }
    // What can be done to every window of a stack at once.
    function stackEntries(windows, tasks) {
        var minimized = windows.every(function(w) { return w.minimized })
        return [minimized
                ? { text: "Restore all", icon: "app-window",
                    run: function() { windows.forEach(function(w) { tasks.activate(w.taskId) }) } }
                : { text: "Minimize all", icon: "minus",
                    run: function() {
                        windows.forEach(function(w) { if (!w.minimized) tasks.minimize(w.taskId) })
                    } }].concat(WindowMenu.placeEntries(windows, tasks))
    }
    // What can be done to the window `window` (its roles, as menuWindows lists them), labelled
    // by its state, through `tasks`, the panel's source of windows: a minimized one is only
    // restored.
    function windowEntries(window, tasks) {
        var id = window.taskId
        var entries = window.minimized
            ? [{ text: "Restore", icon: "app-window", run: function() { tasks.activate(id) } }]
            : [{ text: "Minimize", icon: "minus", run: function() { tasks.minimize(id) } },
               { text: window.maximized ? "Restore" : "Maximize", icon: window.maximized ? "copy" : "square",
                 run: function() { tasks.maximize(id) } },
               { text: "Fullscreen", toggle: "check", checked: window.fullscreen === true,
                 run: function() { tasks.setFullscreen(id, window.fullscreen !== true) } }]
        entries = entries.concat(WindowMenu.placeEntries([window], tasks))
        if (window.workspace > 0 && shell.stickyWindows)
            entries.push({ text: "Keep on all workspaces", toggle: "check", checked: window.sticky === true,
                           run: function() { tasks.setSticky(id, window.sticky !== true) } })
        entries = entries.concat(WindowMenu.aboveEntries(window, tasks, "Keep above others", "contextMenuAbove"))
        // Only where windows tile is there a tiling to leave.
        if (window.workspace > 0 && window.tiling === true)
            entries.push({ text: "Float", toggle: "check", checked: window.floating === true,
                           run: function() { tasks.setFloating(id, window.floating !== true) } })
        return entries
    }
    // The groups of entries, with a line between those that have any.
    function sections(groups) {
        var entries = []
        for (var i = 0; i < groups.length; ++i) {
            if (groups[i].length === 0)
                continue
            if (entries.length > 0)
                entries.push({ separator: true })
            entries = entries.concat(groups[i])
        }
        return entries
    }

    // A task menu acts on the windows through the panel's source of them, which the tests replace
    // with one that notes what it is asked.
    entries: {
        if (panel.taskMenuId >= 0)
            return panel.macos ? dockEntries(panel.taskMenuId, panel.taskSource) : taskEntries(panel.taskMenuId, panel.taskSource)
        // A pinned application without windows; on the dock without a title, as macOS's.
        var app = panel.pinMenuApp
        if (app !== null)
            return panel.macos ? sections([bare(launchEntries(app, "Open")), app.configured ? [] : [panel.pinAction(app.appId)]])
                               : sections([[titleEntry(app, app.appId, "")], launchEntries(app, "Open"),
                                           app.configured ? [] : [panel.pinAction(app.appId)]])
        // Icons of what each entry leads to: floating windows or tiles, as the tiling button shows.
        return [{ text: panel.tiling ? "Turn tiling off" : "Turn tiling on",
                  icon: panel.tiling ? "copy" : "layout-panel-left", enabled: shell.tilingAvailable,
                  run: function() { shell.toggleTiling(contextMenu.panel.outputName) } },
                { text: "Applications", icon: "layout-grid", run: function() { contextMenu.panel.launcherOpen = true } },
                { text: "Show desktop", icon: "minimize-2", run: function() { shell.tasks.showDesktop() } }]
            .concat(shell.profiles.length > 0
                ? [{ text: "Appearance", icon: "palette", secondary: shell.profile,
                     submenu: shell.profiles.map(function(name) {
                         return { text: name, toggle: "radio", checked: name === shell.profile,
                                  run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
                     }) }]
                : [])
    }
}
