// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The clock's flyout, as on Windows 11: the month calendar on a card at the bar's right end, and
// the notifications on another beyond it, away from the bar. A card is left out while it has
// nothing to show: the notifications while the shell serves none, the calendar with
// shell.widgets.calendar off. Where the output is too short for both, the calendar leaves out its
// time and date to give the notifications the room, and they scroll in what is left.
//
// In the macOS style it is Notification Center: the notifications nearest the clock, each on a card
// of its own, and the calendar under them, without the time the menu bar shows; they scroll in
// the room the calendar leaves.
Item {
    id: flyout
    required property var panel
    parent: panel.popupLayer
    anchors.fill: parent
    objectName: "clockFlyout"
    readonly property bool open: panel.audioPopup === "clock"
    readonly property bool showCalendar: shell.widgets.calendar
    readonly property bool showNotifications: shell.notifications.serving
    // The bar's right end, which the cards line up with.
    readonly property rect barEnd: panel.barAnchor(panel.statusBar.x + panel.statusBar.width, 0)
    readonly property Item calendar: calendar
    readonly property Item notifications: notifications

    // The room above the bar for both cards, the gap between them and their margins.
    readonly property real room: panel.popupArea.height - 2 * calendar.margin - notifications.gap
    CalendarPopup {
        id: calendar
        panel: flyout.panel
        open: flyout.open && flyout.showCalendar
        anchorRect: Theme.macos && flyout.showNotifications
            ? Qt.rect(notifications.x, notifications.y, notifications.width, notifications.height)
            : flyout.barEnd
        compact: Theme.macos || flyout.showNotifications && calendar.fullHeight + notifications.implicitHeight > flyout.room
    }
    NotificationHistory {
        id: notifications
        panel: flyout.panel
        open: flyout.open && flyout.showNotifications
        implicitWidth: calendar.implicitWidth
        anchorRect: flyout.showCalendar && !Theme.macos ? Qt.rect(calendar.x, calendar.y, calendar.width, calendar.height)
                                                        : flyout.barEnd
        maximumHeight: Theme.macos && flyout.showCalendar ? flyout.room - calendar.implicitHeight : Infinity
    }
}
