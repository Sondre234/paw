// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "WindowMenu.js" as WindowMenu

// The profile button's popup: the appearance profiles, the one in use marked.
PopupMenu {
    id: profileList
    required property var panel
    parent: panel.popupLayer
    objectName: "profileList"
    entryName: "profileItem"
    open: panel.audioPopup === "profiles"
    // Centred below its item, or from its left edge as macOS's menus open.
    anchorRect: panel.barAnchor(panel.audioPopupX - panel.audioPopupWidth / 2, panel.audioPopupWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    minimumWidth: 240
    onDismissed: panel.audioPopup = ""
    entries: [{ header: "Appearance" }].concat(WindowMenu.profileEntries())
}
