// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The time and date. Clicking it opens the clock flyout: the notifications and the month
// calendar. Beside the time, how many notifications have not been seen, and a crossed-out bell
// while do-not-disturb is on, which a right-click toggles.
FlatButton {
    id: clockButton
    required property var panel
    objectName: "clockButton"
    readonly property var center: shell.notifications
    readonly property int unread: center.serving ? center.unread : 0
    readonly property string status: !center.serving ? ""
        : (center.dnd ? "Do not disturb, " : "") +
          (unread > 0 ? unread + (unread === 1 ? " unread notification" : " unread notifications") : "No new notifications")
    visible: shell.widgets.clock
    Layout.preferredWidth: content.implicitWidth + 2 * Theme.spacingM; Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    enabled: shell.widgets.calendar || center.serving
    onClicked: clockButton.panel.toggleAudioPopup("clock", clockButton)
    Accessible.name: Qt.formatDateTime(clock.now, "dddd d MMMM yyyy, HH:mm") + (status ? ", " + status : "")
    active: clockButton.panel.audioPopup === "clock"
    BarTip {
        panel: clockButton.panel; owner: clockButton
        text: Qt.formatDate(clock.now, "dddd d MMMM yyyy") +
              (clockButton.status ? "\n" + clockButton.status + "\nRight-click: do not disturb" : "")
    }
    MouseArea {
        anchors.fill: parent
        enabled: clockButton.center.serving
        acceptedButtons: Qt.RightButton
        onPressed: clockButton.center.toggleDnd()
    }
    // The compositor's notification_history action opens the flyout on the output under the
    // pointer, whether or not the clock is on the bar.
    Connections {
        target: shell
        function onNotificationsRequested(output) {
            if (output === clockButton.panel.outputName) clockButton.panel.toggleAudioPopup("clock", clockButton)
        }
    }
    contentItem: Item {
        Row {
            id: content
            anchors.centerIn: parent
            spacing: Theme.spacingS
            Text {
                id: clock
                objectName: "clock"
                property date now: new Date()
                anchors.verticalCenter: parent.verticalCenter
                // The macOS style's menu bar has the date before the time, with the day and the
                // month in the order the locale writes them: "Tue 6 Oct  14:32".
                readonly property string shortDate: Qt.locale().dateFormat(Locale.ShortFormat)
                readonly property string dateFormat: shortDate.indexOf("d") < shortDate.indexOf("M") ? "ddd d MMM" : "ddd MMM d"
                text: Theme.macos ? Qt.formatDate(now, dateFormat) + "  " + Qt.formatTime(now, "HH:mm") : Qt.formatTime(now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontSize; font.weight: Theme.macos ? Font.Normal : Font.DemiBold; font.family: Theme.fontFamily
                // The clock shows minutes, so it wakes once a minute, just after the minute changes.
                Timer {
                    id: tick
                    objectName: "clockTick"
                    function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
                    interval: untilMinute(); running: true; repeat: true
                    onTriggered: { clock.now = new Date(); interval = untilMinute() }
                }
            }
            Icon {
                objectName: "clockDnd"
                visible: clockButton.center.serving && clockButton.center.dnd
                anchors.verticalCenter: parent.verticalCenter
                name: "bell-off"; size: Theme.iconSizeSmall; color: Theme.textMuted
            }
            // The unread count, in the accent colour, or muted while do-not-disturb holds the
            // cards back.
            Badge {
                objectName: "clockBadge"
                anchors.verticalCenter: parent.verticalCenter
                count: clockButton.unread
                muted: clockButton.center.dnd
            }
        }
    }
}
