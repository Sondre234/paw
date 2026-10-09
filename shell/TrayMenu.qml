// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A tray item's menu (DBusMenu), in the style of the bar's own, above its icon: separators, check
// boxes, radio buttons, icons, greyed-out entries, and submenus beside their entries. A long one
// scrolls.
PopupMenu {
    id: trayMenu
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "trayMenu"
    entryName: "trayMenuItem"
    separatorName: "trayMenuSeparator"
    open: panel.trayMenuKey !== ""
    entries: panel.trayEntries(panel.trayMenuKey, 0, panel.trayMenuRevision)
    // Centred below its item, or from its left edge as macOS's menus open.
    anchorRect: panel.barAnchor(panel.trayMenuX - panel.trayMenuWidth / 2, panel.trayMenuWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 180
    onDismissed: panel.trayMenuKey = ""
    // The application hears of every level that opens and closes.
    onOpenEntriesChanged: Qt.callLater(panel.syncTrayMenu)
    // Another item's menu taking this one's place starts without the last one's submenus.
    Connections {
        target: trayMenu.panel
        function onTrayMenuKeyChanged() { trayMenu.closeSubmenus() }
    }
}
