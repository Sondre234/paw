// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock flyout's calendar card (ClockFlyout.qml): the time and today's date over a month
// calendar with today marked, paged by month. Weeks start on the locale's first day. As on
// Windows, the title zooms out to the year's months and then to a decade's years, where a pick
// zooms back in. In the macOS style it is Notification Center's, as wide as it and with shorter
// rows.
PopupCard {
    id: calendar
    required property var panel
    objectName: "calendar"
    property int month: new Date().getMonth()
    property int year: new Date().getFullYear()
    // What the grid shows: "days" of the month, the "months" of the year, or the "years" of its
    // decade.
    property string view: "days"
    // Without the time and today's date over the month, which the bar shows: for a short
    // output, where the notifications over it want the room. `fullHeight` is its height with
    // them, whether or not it is compact.
    property bool compact: false
    readonly property real fullHeight: 2 * padding + today.implicitHeight + 1 + navigation.implicitHeight +
                                       pages.Layout.preferredHeight + 3 * content.spacing
    readonly property int decade: Math.floor(year / 10) * 10
    // Now, to the minute, while it shows.
    property date now: new Date()
    // How far the grid still has to slide in after paging, from 1 (from below) or -1 (from
    // above) to 0, and how far a new view has zoomed in, from 0 to 1, starting at zoomFrom times
    // its size.
    property real shift: 0
    property real zoom: 1
    property real zoomFrom: 1
    function slideIn(later) {
        if (later !== 0) {
            slide.direction = later > 0 ? 1 : -1
            slide.restart()
        }
    }
    // Shows another month, sliding it in from the side it lies on: a later one from below.
    function show(newYear, newMonth) {
        var later = newYear * 12 + newMonth - (year * 12 + month)
        year = newYear; month = newMonth
        slideIn(later)
    }
    // Pages by a month, a year or a decade, as the view goes.
    function step(delta) {
        if (view === "days") {
            var d = new Date(year, month + delta, 1)
            show(d.getFullYear(), d.getMonth())
        } else {
            var years = view === "months" ? delta : 10 * delta
            year += years
            slideIn(years)
        }
    }
    // Zooms out (to a longer span) or in to another view: the new one shrinks or grows into place.
    function zoomTo(next) {
        var order = ["days", "months", "years"]
        zoomFrom = order.indexOf(next) > order.indexOf(view) ? 1.15 : 0.85
        view = next
        zooming.restart()
    }
    function today() {
        var d = new Date()
        if (view === "days") {
            show(d.getFullYear(), d.getMonth())
        } else {
            year = d.getFullYear(); month = d.getMonth()
            zoomTo("days")
        }
    }
    onOpened: {
        now = new Date()
        year = now.getFullYear(); month = now.getMonth()
        view = "days"
        slide.stop(); zooming.stop()
        shift = 0; zoom = 1
    }
    NumberAnimation {
        id: slide
        property real direction: 1
        target: calendar; property: "shift"
        from: direction; to: 0
        duration: Theme.durationNormal; easing.type: Theme.easing
    }
    NumberAnimation {
        id: zooming
        target: calendar; property: "zoom"
        from: 0; to: 1
        duration: Theme.durationNormal; easing.type: Theme.easing
    }
    // A day's cell, and the room around the grid.
    readonly property real padding: Theme.macos ? Theme.spacingL + Theme.spacingXS : Theme.spacingXL
    readonly property real cellWidth: Theme.macos ? Math.floor((Theme.notificationCenterWidth - 2 * padding) / 7)
                                                  : Theme.rowHeight + Theme.spacingL
    readonly property real cellHeight: Theme.macos ? Theme.menuRowHeight + Theme.spacingS : Theme.rowHeight + Theme.spacingS
    implicitWidth: 7 * cellWidth + 2 * padding
    implicitHeight: content.implicitHeight + 2 * padding
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    Timer {
        function untilMinute() { var d = new Date(); return 60050 - d.getSeconds() * 1000 - d.getMilliseconds() }
        interval: untilMinute(); running: calendar.visible; repeat: true
        onTriggered: { calendar.now = new Date(); interval = untilMinute() }
    }

    // A month or a year to pick in the zoomed-out views: a disc in the accent colour for this
    // month or year, muted outside the decade shown.
    component Pick: AbstractButton {
        id: pick
        property bool current: false
        property bool dim: false
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height) - 2 * Theme.spacingXL; height: width; radius: width / 2
                color: pick.current ? (pick.hovered ? Theme.accentHover : Theme.accent)
                       : pick.pressed ? Theme.pressed : pick.hovered ? Theme.hover : "transparent"
            }
        }
        contentItem: Text {
            text: pick.text
            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            color: pick.current ? Theme.textOnAccent : pick.dim ? Theme.textDisabled : Theme.text
            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
            font.weight: pick.current ? Font.DemiBold : Font.Normal
        }
    }

    ColumnLayout {
        id: content
        anchors.fill: parent; anchors.margins: calendar.padding
        spacing: Theme.spacingM
        // A wheel notch pages once: down or right to what comes next.
        WheelHandler {
            // Qt takes the whole pointer for a touchpad once the compositor offers gestures
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            property real travel: 0
            onWheel: (event) => {
                travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
                var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
                travel -= steps * 120
                if (steps !== 0)
                    calendar.step(-steps)
            }
        }
        // The time, large, and today's date under it.
        ColumnLayout {
            id: today
            visible: !calendar.compact
            Layout.fillWidth: true; Layout.leftMargin: Theme.spacingS
            spacing: 0
            Text {
                objectName: "calendarTime"
                text: Qt.formatTime(calendar.now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontSizeDisplay; font.weight: Font.Light; font.family: Theme.fontFamily
            }
            Text {
                objectName: "calendarDate"
                text: Qt.formatDate(calendar.now, "dddd d MMMM yyyy")
                color: Theme.accent
                font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
        }
        Rectangle { visible: !calendar.compact; Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.divider }
        // What is shown, which zooms out when clicked, and the arrows to page through it.
        RowLayout {
            id: navigation
            Layout.fillWidth: true; spacing: Theme.spacingXS
            FlatButton {
                id: title
                objectName: "calendarTitle"
                Layout.preferredHeight: Theme.rowHeight
                leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                enabled: calendar.view !== "years"
                onClicked: calendar.zoomTo(calendar.view === "days" ? "months" : "years")
                Accessible.name: calendar.view === "days" ? "Choose a month" : "Choose a year"
                contentItem: Text {
                    text: calendar.view === "days" ? Qt.locale().standaloneMonthName(calendar.month) + " " + calendar.year
                        : calendar.view === "months" ? String(calendar.year)
                        : calendar.decade + "–" + (calendar.decade + 9)
                    color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "calendarToday"
                Layout.preferredHeight: Theme.rowHeight - Theme.spacingS
                text: "Today"
                enabled: calendar.view !== "days" || calendar.month !== calendar.now.getMonth() ||
                         calendar.year !== calendar.now.getFullYear()
                onClicked: calendar.today()
                Accessible.name: "Go to today"
            }
            FlatButton {
                objectName: "calendarPrevious"
                Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
                onClicked: calendar.step(-1)
                Accessible.name: calendar.view === "days" ? "Previous month" : calendar.view === "months" ? "Previous year" : "Previous decade"
                contentItem: Item { Icon { anchors.centerIn: parent; name: "chevron-right"; rotation: -90; size: Theme.iconSize } }
            }
            FlatButton {
                objectName: "calendarNext"
                Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
                onClicked: calendar.step(1)
                Accessible.name: calendar.view === "days" ? "Next month" : calendar.view === "months" ? "Next year" : "Next decade"
                contentItem: Item { Icon { anchors.centerIn: parent; name: "chevron-right"; rotation: 90; size: Theme.iconSize } }
            }
        }
        // The views, one at a time, in the same room. The one shown zooms in from zoomFrom times
        // its size and fades in, and what it lists slides in when paged.
        Item {
            id: pages
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.headingHeight + 6 * calendar.cellHeight
            clip: true
            readonly property real zoomScale: calendar.zoomFrom + (1 - calendar.zoomFrom) * calendar.zoom
            Item {
                id: days
                anchors.fill: parent
                visible: calendar.view === "days"
                scale: pages.zoomScale; opacity: calendar.zoom
                // Both size their cells only when their own size changes, which can happen
                // before the cells exist when the popup is made ahead of use; size them here.
                DayOfWeekRow {
                    id: weekRow
                    width: parent.width; height: Theme.headingHeight
                    locale: Qt.locale()
                    spacing: 0
                    delegate: Text {
                        required property string shortName
                        // A letter in the macOS style, as its calendar has them.
                        required property string narrowName
                        width: weekRow.availableWidth / 7; height: weekRow.availableHeight
                        text: Theme.macos ? narrowName : shortName; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                }
                Item {
                    y: Theme.headingHeight
                    width: parent.width; height: 6 * calendar.cellHeight
                    clip: true
                    MonthGrid {
                        id: monthGrid
                        objectName: "monthGrid"
                        width: parent.width; height: parent.height
                        y: calendar.shift * calendar.cellHeight
                        opacity: 1 - Math.abs(calendar.shift)
                        month: calendar.month; year: calendar.year
                        locale: Qt.locale()
                        spacing: 0
                        delegate: Item {
                            id: dayCell
                            required property var model
                            width: monthGrid.availableWidth / 7; height: monthGrid.availableHeight / 6
                            readonly property bool inMonth: model.month === monthGrid.month
                            readonly property bool isToday: model.today && inMonth
                            HoverHandler { id: dayHover }
                            Rectangle {
                                anchors.centerIn: parent
                                width: Math.min(parent.width, parent.height) - Theme.spacingXS; height: width; radius: width / 2
                                visible: dayCell.isToday || dayHover.hovered
                                color: dayCell.isToday ? (dayHover.hovered ? Theme.accentHover : Theme.accent) : Theme.hover
                            }
                            Text {
                                anchors.centerIn: parent
                                text: dayCell.model.day
                                color: dayCell.isToday ? Theme.textOnAccent : dayCell.inMonth ? Theme.text : Theme.textDisabled
                                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                font.weight: dayCell.isToday ? Font.DemiBold : Font.Normal
                            }
                        }
                    }
                }
            }
            // The year's months; picking one shows its days.
            Item {
                objectName: "calendarMonths"
                anchors.fill: parent
                visible: calendar.view === "months"
                scale: pages.zoomScale; opacity: calendar.zoom * (1 - Math.abs(calendar.shift))
                transform: Translate { y: calendar.shift * Theme.rowHeight }
                Repeater {
                    model: 12
                    Pick {
                        required property int index
                        readonly property int month: index
                        objectName: "calendarMonth"
                        // Four to a row, three rows.
                        x: index % 4 * width; y: Math.floor(index / 4) * height
                        width: pages.width / 4; height: pages.height / 3
                        text: Qt.locale().standaloneMonthName(index, Locale.ShortFormat)
                        current: index === calendar.now.getMonth() && calendar.year === calendar.now.getFullYear()
                        Accessible.name: Qt.locale().standaloneMonthName(index) + " " + calendar.year
                        onClicked: {
                            calendar.month = index
                            calendar.zoomTo("days")
                        }
                    }
                }
            }
            // The decade's years, with the one before and the one after; picking one shows its
            // months.
            Item {
                objectName: "calendarYears"
                anchors.fill: parent
                visible: calendar.view === "years"
                scale: pages.zoomScale; opacity: calendar.zoom * (1 - Math.abs(calendar.shift))
                transform: Translate { y: calendar.shift * Theme.rowHeight }
                Repeater {
                    model: 12
                    Pick {
                        required property int index
                        readonly property int year: calendar.decade - 1 + index
                        objectName: "calendarYear"
                        // Four to a row, three rows.
                        x: index % 4 * width; y: Math.floor(index / 4) * height
                        width: pages.width / 4; height: pages.height / 3
                        text: String(year)
                        current: year === calendar.now.getFullYear()
                        dim: index === 0 || index === 11
                        onClicked: {
                            calendar.year = year
                            calendar.zoomTo("months")
                        }
                    }
                }
            }
        }
    }
}
