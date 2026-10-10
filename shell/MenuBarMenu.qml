// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import Paw
import "WindowMenu.js" as WindowMenu

// The menus of the macOS style's menu bar, one at a time below its title (panel.menuBarMenu):
// - the system menu: the appearance profiles, the wallpaper picker, the display settings window,
//   and the power actions the compositor allows, those that end the session asking first
//   (PowerDialog.qml);
// - the focused application's: starting it again or one of its desktop actions, hiding it (or
//   the others) and quitting it, which close its windows; while no window is focused, what the
//   desktop offers;
// - the Window menu: what can be done to the focused window, where it can go, and its
//   application's other windows.
// Going from one menu to another changes the entries in place, as on macOS; Left and Right go to
// the menu beside, unless a submenu is there to close or open.
PopupMenu {
    id: menuBarMenu
    required property var panel
    parent: panel.popupLayer
    objectName: "menuBarMenu"
    entryName: "menuBarMenuItem"
    open: panel.menuBarMenu !== ""
    anchorRect: panel.barAnchor(panel.menuBarMenuX, 0)
    side: panel.popupSide
    alignment: Qt.AlignLeft
    gap: Theme.spacingXS
    bounds: panel.popupArea
    minimumWidth: 220
    onDismissed: panel.menuBarMenu = ""

    readonly property Item bar: panel.menuBar
    // The window the application's menu and the Window menu are about (TopMenuBar.qml), and every
    // window of its application.
    readonly property var window: bar ? bar.focusedWindow : null
    TaskFilter {
        id: appWindows
        readonly property string slot: menuBarMenu.window ? shell.pinnedAppFor(menuBarMenu.window.appId) : ""
        controller: shell
        sourceModel: menuBarMenu.window ? menuBarMenu.panel.taskSource : null
        app: slot
        windowApp: slot === "" && menuBarMenu.window ? menuBarMenu.window.appId : ""
        // One without an app id stands alone.
        taskId: menuBarMenu.window && menuBarMenu.window.appId === "" ? menuBarMenu.window.taskId : -1
    }
    TaskFilter { id: everyWindow; sourceModel: menuBarMenu.panel.taskSource }

    // A change of menu starts it afresh: no submenu open, nothing highlighted.
    Connections {
        target: menuBarMenu.panel
        function onMenuBarMenuChanged() {
            menuBarMenu.closeSubmenus()
            menuBarMenu.card.current = -1
        }
    }
    Keys.onLeftPressed: (event) => step(-1, event)
    Keys.onRightPressed: (event) => step(1, event)
    function step(direction, event) {
        var level = deepest()
        if (bar === null || (direction < 0 && openEntries.length > 0) ||
            (direction > 0 && level.current >= 0 && level.hasSubmenu(level.current))) {
            event.accepted = false
            return
        }
        var kinds = ["system", "app", "window"], titles = [bar.systemButton, bar.appButton, bar.windowButton]
        var next = (kinds.indexOf(panel.menuBarMenu) + direction + kinds.length) % kinds.length
        panel.toggleMenuBarMenu(kinds[next], titles[next])
    }

    // The groups of entries, with a line between those that have any, and no icons, as macOS's
    // menus have none.
    function sections(groups) {
        var entries = []
        for (var i = 0; i < groups.length; ++i) {
            if (groups[i].length === 0)
                continue
            if (entries.length > 0)
                entries.push({ separator: true })
            entries = entries.concat(groups[i].map(plain))
        }
        return entries
    }
    function plain(entry) {
        if (!entry.icon)
            return entry
        var copy = Object.assign({}, entry)
        delete copy.icon
        return copy
    }

    function systemEntries() {
        var look = []
        if (shell.profiles.length > 0)
            look.push({ text: "Appearance", secondary: shell.profile, objectName: "systemMenuAppearance",
                        submenu: shell.profiles.map(function(name) {
                            return { text: name, toggle: "radio", checked: name === shell.profile,
                                     run: function() { if (name !== shell.profile) shell.pickProfile(name) } }
                        }) })
        if (shell.wallpaperFolder !== "")
            look.push({ text: "Wallpaper…", objectName: "systemMenuWallpaper",
                        run: function() { panel.toggleAudioPopup("wallpapers", bar.systemButton) } })
        look.push({ text: "Displays…", objectName: "systemMenuDisplays",
                    run: function() { panel.closeMenus(); shell.displaySettings.show(panel.outputName) } })
        // As macOS orders them, by what the compositor allows; the popover gives the keyboard back
        // before one runs.
        var titles = { suspend: "Sleep", hibernate: "Hibernate", reboot: "Restart…", poweroff: "Shut Down…",
                       lock: "Lock Screen", logout: "Log Out…" }
        var allowed = shell.power.entries.map(function(entry) { return entry.action })
        function power(actions) {
            return actions.filter(function(action) { return allowed.indexOf(action) >= 0 }).map(function(action) {
                return { text: titles[action], objectName: "systemMenu:" + action,
                         run: function() { panel.closeMenus(); shell.power.request(action, panel.outputName) } }
            })
        }
        return sections([look, power(["suspend", "hibernate", "reboot", "poweroff"]), power(["lock", "logout"])])
    }

    function appEntries() {
        var tasks = panel.taskSource
        if (!window)
            return [{ text: "Applications", objectName: "appMenuApplications", run: function() { panel.launcherOpen = true } },
                    { text: "Show Desktop", run: function() { shell.tasks.showDesktop() } },
                    { separator: true },
                    { text: panel.tiling ? "Turn Tiling Off" : "Turn Tiling On", enabled: shell.tilingAvailable,
                      run: function() { shell.toggleTiling(panel.outputName) } }]
        var record = bar.appRecord, name = bar.appName, windows = appWindows.windows
        // What its desktop entry starts besides itself, then a new window, unless an action of
        // its own is one.
        var launches = []
        if (record) {
            var actions = shell.appActions(record.appId)
            launches = actions.map(function(action) {
                return { text: action.name, objectName: "appMenuAction",
                         run: function() { shell.launchAction(record.appId, action.action) } }
            })
            if (!actions.some(function(action) { return action.action === "new-window" || action.name.toLowerCase() === "new window" }))
                launches.unshift({ text: "New Window", objectName: "appMenuNewWindow",
                                   run: function() { shell.launch(record.appId) } })
        }
        var mine = windows.map(function(w) { return w.taskId })
        var others = everyWindow.windows.filter(function(w) {
            return mine.indexOf(w.taskId) < 0 && !w.minimized && (!w.output || w.output === panel.outputName)
        })
        return sections([launches,
                         [{ text: "Hide " + name, objectName: "appMenuHide",
                            run: function() { windows.forEach(function(w) { if (!w.minimized) tasks.minimize(w.taskId) }) } },
                          { text: "Hide Others", objectName: "appMenuHideOthers", enabled: others.length > 0,
                            run: function() { others.forEach(function(w) { tasks.minimize(w.taskId) }) } }],
                         [{ text: "Quit " + name, objectName: "appMenuQuit",
                            run: function() { windows.forEach(function(w) { tasks.close(w.taskId) }) } }]])
    }

    function windowEntries() {
        var w = window, tasks = panel.taskSource, id = w ? w.taskId : -1
        // Snapping is the compositor's, for the focused window, which this one is once activated.
        function snap(action) {
            return function() { tasks.activate(id); shell.send(action) }
        }
        var state = [{ text: "Minimize", objectName: "windowMenu:minimize", enabled: !!w && !w.minimized,
                       run: function() { tasks.minimize(id) } },
                     { text: "Zoom", objectName: "windowMenu:zoom", enabled: !!w,
                       run: function() { tasks.maximize(id) } }]
        var tile = [{ text: "Tile Window to Left of Screen", objectName: "windowMenu:left", enabled: !!w, run: snap("snap_left") },
                    { text: "Tile Window to Right of Screen", objectName: "windowMenu:right", enabled: !!w, run: snap("snap_right") }]
        var full = [{ text: w && w.fullscreen ? "Exit Full Screen" : "Enter Full Screen", objectName: "windowMenu:fullscreen",
                      enabled: !!w, run: function() { tasks.setFullscreen(id, !w.fullscreen) } }]
        if (!w)
            return sections([state, tile, full])
        var place = []
        // Only where windows tile is there a tiling to leave.
        if (w.workspace > 0 && w.tiling === true)
            place.push({ text: "Float", toggle: "check", checked: w.floating === true, objectName: "windowMenu:float",
                         run: function() { tasks.setFloating(id, w.floating !== true) } })
        if (w.workspace > 0 && shell.stickyWindows)
            place.push({ text: "Keep on All Workspaces", toggle: "check", checked: w.sticky === true, objectName: "windowMenu:sticky",
                         run: function() { tasks.setSticky(id, w.sticky !== true) } })
        place = place.concat(WindowMenu.aboveEntries(w, tasks, "Keep Above Others", "windowMenu:above"))
        place = place.concat(WindowMenu.placeEntries([w], tasks))
        // Its application's windows, to bring one up, the focused one marked.
        var windows = appWindows.windows.length > 1 ? appWindows.windows.map(function(other) {
            return { text: other.title || bar.appName, toggle: "check", checked: other.taskId === id,
                     objectName: "windowMenuWindow", run: function() { tasks.activate(other.taskId) } }
        }) : []
        return sections([state, tile, full, place, windows,
                         [{ text: "Close Window", objectName: "windowMenu:close", run: function() { tasks.close(id) } }]])
    }

    // The menu shown, kept while it fades out.
    property string kind: ""
    Binding on kind {
        when: menuBarMenu.panel.menuBarMenu !== ""
        value: menuBarMenu.panel.menuBarMenu
        restoreMode: Binding.RestoreNone
    }
    entries: kind === "system" ? systemEntries() : kind === "app" ? appEntries() : kind === "window" ? windowEntries() : []
}
