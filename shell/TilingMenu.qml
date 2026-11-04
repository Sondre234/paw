// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The tiling button's menu, opened by a right click: what the workspace this monitor shows does
// now, floating or the layout it tiles with, as its title, then the others to switch to, each with
// an icon of how it lays windows out. A layout chosen while the workspace floats turns tiling on
// with it, and Floating turns tiling off. Below them, Arrangements: the saved sessions, one of
// which a click restores (launching what of it is not running), and saving the windows as they
// are as a new one (ArrangementPrompt.qml), over one, renaming one and deleting one.
PopupMenu {
    id: tilingMenu
    required property var panel
    parent: panel.popupLayer
    objectName: "tilingMenu"
    entryName: "tilingMenuItem"
    open: panel.audioPopup === "tiling"
    // Centred on the button, or from its left edge as macOS's menus open.
    anchorRect: panel.barAnchor(panel.audioPopupX - panel.audioPopupWidth / 2, panel.audioPopupWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 180
    onDismissed: panel.audioPopup = ""
    // The saved sessions may have changed since the menu last opened (`paw msg session save`).
    onOpenChanged: if (open) shell.readSessions()
    Component.onCompleted: if (open) shell.readSessions()

    // The layouts in the order layout_next steps through them.
    readonly property var layouts: ["dwindle", "master", "spiral", "monocle", "scroll"]
    readonly property string layout: (shell.workspaces[panel.outputName] || {}).layout || ""

    function label(name) { return name.charAt(0).toUpperCase() + name.slice(1) }
    // Each saved session as an entry that does `run` with its name.
    function named(objectName, run, danger) {
        return shell.sessions.map(function(session) {
            return { text: session.name, danger: danger, objectName: objectName,
                     run: function() { return run(session.name) } }
        })
    }
    function arrangements() {
        var saved = shell.sessions.map(function(session) {
            return { text: session.name, icon: "layout-grid", objectName: "arrangementRestore",
                     secondary: session.windows + (session.windows === 1 ? " window" : " windows"),
                     run: function() { shell.restoreSession(session.name) } }
        })
        if (saved.length === 0)
            saved = [{ text: "None saved yet", enabled: false }]
        var manage = [{ separator: true },
                      { text: "Save current arrangement…", icon: "plus", objectName: "arrangementSave",
                        run: function() { return tilingMenu.panel.promptArrangement("") } }]
        if (shell.sessions.length > 0)
            manage = manage.concat([
                { text: "Update", icon: "rotate-ccw", objectName: "arrangementUpdateMenu",
                  submenu: function() { return tilingMenu.named("arrangementUpdate", function(name) { shell.saveSession(name) }, false) } },
                { text: "Rename", icon: "pencil", objectName: "arrangementRenameMenu",
                  submenu: function() { return tilingMenu.named("arrangementRename", function(name) { return tilingMenu.panel.promptArrangement(name) }, false) } },
                { text: "Delete", icon: "trash-2", objectName: "arrangementDeleteMenu",
                  submenu: function() { return tilingMenu.named("arrangementDelete", function(name) { shell.deleteSession(name) }, true) } }])
        return saved.concat(manage)
    }
    entries: {
        var tiling = panel.tiling, output = panel.outputName
        var title = tiling ? { title: label(layout), icon: "tiling-" + layout, secondary: "Tiling layout" }
                           : { title: "Floating", icon: "copy", secondary: "Tiling off" }
        title.objectName = "tilingMenuTitle"
        var others = layouts.filter(function(name) { return !tiling || name !== layout }).map(function(name) {
            return { text: label(name), icon: "tiling-" + name,
                     run: function() { shell.tileWith(output, name, !tiling) } }
        })
        return [title, { separator: true }].concat(others).concat(tiling
            ? [{ separator: true },
               { text: "Floating", icon: "copy", run: function() { shell.toggleTiling(output) } }]
            : []).concat([
               { separator: true },
               { text: "Arrangements", icon: "layers", objectName: "arrangementsMenu",
                 submenu: function() { return tilingMenu.arrangements() } }])
    }
}
