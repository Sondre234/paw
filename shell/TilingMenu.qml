// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The tiling button's menu, opened by a right click: what the workspace this monitor shows does
// now, floating or the layout it tiles with, as its title, then the others to switch to, each with
// an icon of how it lays windows out. A layout chosen while the workspace floats turns tiling on
// with it, and Floating turns tiling off.
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

    // The layouts in the order layout_next steps through them.
    readonly property var layouts: ["dwindle", "master", "spiral", "monocle", "scroll"]
    readonly property string layout: (shell.workspaces[panel.outputName] || {}).layout || ""

    function label(name) { return name.charAt(0).toUpperCase() + name.slice(1) }
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
            : [])
    }
}
