// SPDX-License-Identifier: GPL-3.0-or-later
// The entries of a menu about windows that move them to another workspace or monitor, or keep
// one above the others, as PopupMenu entries, for every menu that has them. `windows` are windows
// as a TaskFilter lists them, `tasks` the panel's source of windows, which carries the moves out.
// And what the menus share besides: their sections, and the appearance profiles to pick from.

// The groups of entries, with a line between those that have any, each entry through `map` if
// one is given.
function sections(groups, map) {
    var entries = []
    for (var i = 0; i < groups.length; ++i) {
        if (groups[i].length === 0)
            continue
        if (entries.length > 0)
            entries.push({ separator: true })
        entries = entries.concat(map ? groups[i].map(map) : groups[i])
    }
    return entries
}
// The appearance profiles, the one in use checked; choosing another switches to it. With
// profile_schedule, the schedule after them, checked while the profile it wants is in use;
// choosing it hands the choice back to it.
function profileEntries() {
    var entries = shell.profiles.map(function(name) {
        return { text: name, toggle: "radio", checked: name === shell.profile,
                 run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
    })
    if (shell.hasProfileSchedule)
        entries.push({ separator: true },
                     { text: "Follow sunrise and sunset", toggle: "check",
                       checked: shell.profileScheduled, objectName: "profileSchedule",
                       run: function() { if (!shell.profileScheduled) shell.send("profile_auto") } })
    return entries
}

// Where the windows can go, once the compositor has said where they are (workspace from 1):
// the workspaces of their monitors, the one they are all on marked.
function placeEntries(windows, tasks) {
    if (windows.length === 0 || !windows.every(function(w) { return w.workspace > 0 }))
        return []
    var entries = []
    if (shell.workspaceCount > 1)
        entries.push({ text: "Move to workspace", icon: "layers", objectName: "contextMenuWorkspaces",
                       submenu: workspaceEntries(windows, tasks) })
    if (Object.keys(shell.workspaces).length > 1)
        entries.push({ text: "Move to monitor", icon: "monitor", objectName: "contextMenuOutputs",
                       submenu: outputEntries(windows, tasks) })
    return entries
}
// The compositor's outputs from left to right, by connector name ("DP-1") with the monitor's
// make and model beside it where the screen says; the one the windows are on marked.
function outputEntries(windows, tasks) {
    var screens = {}
    Qt.application.screens.forEach(function(screen) { screens[screen.name] = screen })
    var names = Object.keys(shell.workspaces).sort(function(a, b) {
        var left = screens[a] ? screens[a].virtualX : 0, right = screens[b] ? screens[b].virtualX : 0
        return left !== right ? left - right : a.localeCompare(b)
    })
    var on = windows.every(function(w) { return w.output === windows[0].output }) ? windows[0].output : ""
    return names.map(function(name) {
        var screen = screens[name]
        return { text: name, toggle: "radio", checked: name === on, objectName: "contextMenuOutput",
                 secondary: screen ? [screen.manufacturer, screen.model].filter(Boolean).join(" ") : "",
                 run: function() { windows.forEach(function(w) { tasks.moveToOutput(w.taskId, name) }) } }
    })
}
function workspaceEntries(windows, tasks) {
    // A sticky window is on all of them.
    var on = windows.every(function(w) { return w.workspace === windows[0].workspace && !w.sticky })
        ? windows[0].workspace : 0
    var names = shell.workspaceNames
    var entries = []
    for (var n = 1; n <= shell.workspaceCount; ++n) {
        entries.push((function(number) {
            return { text: names[number - 1] || "Workspace " + number, toggle: "radio",
                     checked: number === on, objectName: "contextMenuWorkspace",
                     run: function() {
                         windows.forEach(function(w) { tasks.moveToWorkspace(w.taskId, number) })
                     } }
        })(n))
    }
    return entries
}
// Keeping `window` above the others, checked while it is, as `text` in the menu's own style and
// named `objectName`; none until the compositor says where it is, or while it cannot keep a
// window above (its `above` is undefined then).
function aboveEntries(window, tasks, text, objectName) {
    if (!(window.workspace > 0) || window.above === undefined || window.above === null)
        return []
    return [{ text: text, toggle: "check", checked: window.above === true, objectName: objectName,
              run: function() { tasks.setAbove(window.taskId, window.above !== true) } }]
}
// Killing the processes of the application `appId` that run on without a window, which have no
// window to close and may hold its launches, as the taskbar kills a window's: Kill process (Kill
// 2 processes), or with `macos` Force Quit without an icon; none while it has none.
function windowlessEntries(appId, macos, objectName) {
    void shell.windowless // read again as they change
    var count = shell.windowlessPids(appId).length
    if (count === 0)
        return []
    var kill = function() { shell.killWindowless(appId) }
    return [macos ? { text: "Force Quit", objectName: objectName, run: kill }
                  : { text: count > 1 ? "Kill " + count + " processes" : "Kill process",
                      icon: "octagon-x", danger: true, objectName: objectName, run: kill }]
}
