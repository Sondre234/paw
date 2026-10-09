// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The power menu: lock, suspend and the rest, as far as the compositor says they may run, each
// with its icon (the power dialog's for those that ask). It opens with the first entry
// highlighted, for the keyboard: Up and Down choose, Enter runs. Restart, power off and log out
// ask first (PowerDialog.qml).
PopupMenu {
    id: powerMenu
    required property var panel
    objectName: "powerMenu"
    open: panel.powerOpen
    initialIndex: 0
    onDismissed: panel.powerOpen = false
    function run(index) {
        var entry = shell.power.entries[index]
        // Closing the launcher first hands the keyboard back before it runs.
        panel.closeMenus()
        if (entry)
            shell.power.request(entry.action, powerMenu.panel.outputName)
    }
    readonly property var icons: ({ lock: "lock", suspend: "moon", hibernate: "snowflake",
                                    reboot: "rotate-ccw", poweroff: "power", logout: "log-out" })
    entries: shell.power.entries.map(function(entry, index) {
        return { text: entry.title, icon: icons[entry.action] || "", objectName: "powerItem:" + entry.action,
                 run: function() { powerMenu.run(index) } }
    })
}
