// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The clock flyout's notification card (ClockFlyout.qml): every notification still kept, by
// application, the one with the newest first, under a heading with a do-not-disturb switch and a
// button that clears them all. An application with many shows its newest two until asked for the
// rest. A click on a notification runs its default action, and the cross that shows while the
// pointer is over it removes it.
//
// In the macOS style it is Notification Center's: no card of its own, but each notification on a
// card, with its application's icon beside it, the heading on a card above them and an
// application's name and the button for the rest on a pill over its stack.
PopupCard {
    id: history
    required property var panel
    readonly property var center: shell.notifications
    objectName: "notificationHistory"
    framed: !Theme.macos
    // As tall as it may be, for the flyout to leave room for the calendar beside it.
    property real maximumHeight: Infinity
    // Looking at the list is reading it, also when it was made open.
    onOpened: { now = new Date(); expanded = {}; center.markAllRead() }
    Connections { target: history.center; function onUnreadChanged() { if (history.open) history.center.markAllRead() } }
    // Now, for how long ago each came, refreshed while it shows.
    property date now: new Date()
    Timer { interval: 30000; running: history.visible; repeat: true; onTriggered: history.now = new Date() }
    function ago(time) {
        var seconds = (now.getTime() - time.getTime()) / 1000
        if (seconds < 60) return "Now"
        if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
        if (now.toDateString() === time.toDateString()) return Qt.formatTime(time, "HH:mm")
        return Qt.formatDate(time, "d MMM") + " " + Qt.formatTime(time, "HH:mm")
    }
    // An icon for a notification or its application: its image, its own icon, or its
    // application's, else none.
    function iconSource(n) {
        if (n.hasImage) return "image://notify/" + n.notificationId + "/" + Number(n.time)
        if (n.icon.length > 0) return n.icon.charAt(0) === "/" ? "file://" + n.icon : "image://icons/" + n.icon
        var known = n.desktopEntry.length > 0 ? n.desktopEntry : n.app.toLowerCase()
        return shell.appFor(known).length > 0 ? "image://icons/" + shell.iconFor(known) : ""
    }
    readonly property real padding: Theme.macos ? 0 : Theme.spacingL
    // The room inside the macOS style's heading card.
    readonly property real inset: Theme.macos ? Theme.spacingS : 0
    // How many of an application's notifications show until it is expanded, and the applications
    // expanded (by group key) since the card opened.
    readonly property int collapsedCount: 2
    property var expanded: ({})
    function toggleGroup(key) {
        var next = Object.assign({}, expanded)
        if (next[key]) delete next[key]; else next[key] = true
        expanded = next
    }
    implicitHeight: Math.min(maximumHeight, 2 * padding + heading.implicitHeight + inset + layout.spacing +
                                            (center.history.count > 0 ? list.contentHeight : empty.implicitHeight))
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    // The card's colour over the list's edge where it goes on past it, fading into the list over a
    // row's height: from the top down with `fromTop`, else from the bottom up.
    component EdgeFade: Rectangle {
        property bool fromTop: false
        property bool shown: false
        anchors.left: parent.left; anchors.right: parent.right
        height: Theme.rowHeight
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.alpha(history.color, fromTop ? 1 : 0) }
            GradientStop { position: 1; color: Theme.alpha(history.color, fromTop ? 0 : 1) }
        }
    }
    // The macOS style's heading card, which takes the presses beside its buttons.
    Rectangle {
        visible: Theme.macos
        y: layout.y + heading.y - history.inset
        width: parent.width; height: heading.height + 2 * history.inset
        radius: Theme.radiusLarge
        color: Theme.popupSurface
        border.color: Theme.popupOutline
        MouseArea { anchors.fill: parent }
    }
    ColumnLayout {
        id: layout
        anchors.fill: parent; anchors.margins: history.padding
        spacing: Theme.macos ? history.inset + Theme.spacingM : Theme.spacingM
        RowLayout {
            id: heading
            Layout.fillWidth: true; Layout.preferredHeight: Theme.rowHeight
            Layout.leftMargin: Theme.macos ? Theme.spacingL : Theme.spacingS
            Layout.rightMargin: history.inset
            Layout.topMargin: history.inset
            spacing: Theme.spacingM
            Text {
                Layout.fillWidth: true
                text: "Notifications"; elide: Text.ElideRight
                color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Switch {
                id: dnd
                objectName: "dndSwitch"
                text: "Do not disturb"
                checked: history.center.dnd
                onToggled: history.center.dnd = checked
                spacing: Theme.spacingM
                indicator: Rectangle {
                    x: dnd.leftPadding; y: parent.height / 2 - height / 2
                    width: 2 * height; height: Theme.iconSize; radius: height / 2
                    color: dnd.checked ? Theme.accent : Theme.macos ? Theme.switchTrack : Theme.selected
                    Rectangle {
                        x: dnd.checked ? parent.width - width - Theme.spacingXS : Theme.spacingXS
                        y: Theme.spacingXS; width: parent.height - 2 * Theme.spacingXS; height: width; radius: width / 2
                        color: Theme.macos ? Theme.knob : dnd.checked ? Theme.textOnAccent : Theme.text
                        border.color: Theme.macos ? Theme.knobOutline : "transparent"
                        Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    }
                }
                contentItem: Text {
                    leftPadding: dnd.indicator.width + dnd.spacing
                    text: dnd.text; color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    verticalAlignment: Text.AlignVCenter
                }
            }
            TextButton {
                objectName: "clearNotifications"
                Layout.preferredHeight: Theme.rowHeight - Theme.spacingS
                text: "Clear all"
                enabled: history.center.history.count > 0
                onClicked: history.center.clearHistory()
                Accessible.name: "Clear all notifications"
            }
        }
        // Nothing kept: a quiet bell and a line saying so.
        Item {
            id: empty
            objectName: "notificationsEmpty"
            visible: history.center.history.count === 0
            Layout.fillWidth: true; Layout.fillHeight: true
            implicitHeight: emptyState.implicitHeight + 2 * Theme.spacingS
            Rectangle {
                visible: Theme.macos
                anchors.fill: parent
                radius: Theme.radiusLarge
                color: Theme.popupSurface
                border.color: Theme.popupOutline
                MouseArea { anchors.fill: parent }
            }
            EmptyState {
                id: emptyState
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                icon: "bell"
                title: "No new notifications"
            }
        }
        // The list, its edges fading into the card where it goes on past them, so that a card
        // too short for it reads as more to scroll to rather than as cut off.
        Item {
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: history.center.history.count > 0
            ListView {
                id: list
                objectName: "notificationList"
                anchors.fill: parent
                clip: true; spacing: Theme.macos ? Theme.spacingM : Theme.spacingL
                boundsBehavior: Flickable.StopAtBounds
                model: history.visible ? history.center.history.groups : []
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                // An application's heading, its icon and name, and its notifications under it.
                delegate: Column {
                    id: group
                    required property var modelData
                    readonly property var notifications: modelData.notifications
                    readonly property bool collapsible: notifications.length > history.collapsedCount
                    readonly property bool open: !collapsible || history.expanded[modelData.key] === true
                    width: ListView.view.width
                    spacing: Theme.spacingS
                    // In the macOS style on a pill over a stack, and for one notification left out:
                    // its card shows its application's icon.
                    Item {
                        visible: !Theme.macos || group.notifications.length > 1
                        width: parent.width; height: Theme.headingHeight
                        Rectangle {
                            visible: Theme.macos
                            anchors.fill: parent
                            radius: height / 2
                            color: Theme.popupSurface
                            border.color: Theme.popupOutline
                            MouseArea { anchors.fill: parent }
                        }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.macos ? Theme.spacingXS : 0
                            anchors.rightMargin: Theme.macos ? Theme.spacingXS : 0
                            spacing: Theme.spacingM
                            Image {
                                readonly property string icon: history.iconSource(Object.assign({}, group.notifications[0], { hasImage: false }))
                                visible: icon.length > 0
                                Layout.leftMargin: Theme.spacingS
                                Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                                sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                                fillMode: Image.PreserveAspectFit; source: icon; cache: false
                            }
                            Text {
                                Layout.fillWidth: true
                                text: group.modelData.app; textFormat: Text.PlainText; elide: Text.ElideRight
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                            }
                            TextButton {
                                objectName: "groupToggle"
                                visible: group.collapsible
                                Layout.preferredHeight: Theme.headingHeight - Theme.spacingS
                                text: group.open ? "Show less" : (group.notifications.length - history.collapsedCount) + " more"
                                chevron: true
                                chevronRotation: group.open ? -90 : 90
                                onClicked: history.toggleGroup(group.modelData.key)
                            }
                        }
                    }
                    Repeater {
                        model: group.open ? group.notifications : group.notifications.slice(0, history.collapsedCount)
                        delegate: Rectangle {
                            id: row
                            objectName: "notificationRow"
                            required property var modelData
                            readonly property var n: modelData
                            width: group.width
                            height: rowContent.implicitHeight + 2 * Theme.spacingM
                            radius: Theme.macos ? Theme.radiusLarge : Theme.radiusMedium
                            color: Theme.macos ? (rowHover.hovered ? Theme.mix(Theme.popupSurface, Theme.text, 0.04) : Theme.popupSurface)
                                 : rowHover.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
                            border.color: n.urgency === 2 ? Theme.danger : Theme.macos ? Theme.popupOutline : "transparent"
                            HoverHandler { id: rowHover }
                            // Under its buttons, which take their own clicks.
                            MouseArea {
                                anchors.fill: parent
                                onClicked: history.center.activate(row.n.notificationId)
                            }
                            RowLayout {
                                id: rowContent
                                x: Theme.spacingL; y: Theme.spacingM
                                width: parent.width - 2 * Theme.spacingL
                                spacing: Theme.spacingL
                                // The notification's own picture (a sender's face, a screenshot);
                                // in the macOS style else its icon or its application's.
                                Image {
                                    objectName: "notificationImage"
                                    readonly property real size: Theme.macos ? Theme.notificationIconSize : Theme.rowHeight
                                    visible: row.n.hasImage || (Theme.macos && source.toString() !== "")
                                    Layout.alignment: Qt.AlignTop; Layout.topMargin: Theme.spacingXS
                                    Layout.preferredWidth: size; Layout.preferredHeight: size
                                    sourceSize: Qt.size(2 * size, 2 * size)
                                    fillMode: Image.PreserveAspectCrop; cache: false
                                    source: row.n.hasImage || Theme.macos ? history.iconSource(row.n) : ""
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingXS
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: Theme.spacingS
                                        Text {
                                            Layout.fillWidth: true
                                            text: row.n.summary; textFormat: Text.PlainText
                                            elide: Text.ElideRight; maximumLineCount: 2; wrapMode: Text.Wrap
                                            color: Theme.text
                                            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                            font.weight: row.n.read ? Font.DemiBold : Font.Bold
                                        }
                                        Text {
                                            Layout.alignment: Qt.AlignTop
                                            visible: !rowHover.hovered
                                            text: history.ago(row.n.time)
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                                        }
                                        // Shown in the time's place while the pointer is over it.
                                        CloseButton {
                                            objectName: "removeNotification"
                                            Layout.alignment: Qt.AlignTop
                                            Layout.preferredWidth: size; Layout.preferredHeight: size
                                            Layout.topMargin: -Theme.spacingXS
                                            opacity: rowHover.hovered ? 1 : 0
                                            visible: opacity > 0
                                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                                            Accessible.name: "Dismiss"
                                            onClicked: history.center.removeFromHistory(row.n.notificationId)
                                        }
                                    }
                                    Text {
                                        Layout.fillWidth: true; visible: text.length > 0
                                        text: row.n.body
                                        color: Theme.macos ? Theme.text : Theme.textMuted
                                        font.pixelSize: Theme.macos ? Theme.fontSize : Theme.fontSizeSmall
                                        font.family: Theme.fontFamily
                                        textFormat: Text.StyledText; linkColor: Theme.accent
                                        wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
                                        onLinkActivated: (link) => history.center.openLink(link)
                                    }
                                    // The "value" hint, such as a download's progress.
                                    Rectangle {
                                        visible: row.n.progress >= 0
                                        Layout.fillWidth: true; Layout.topMargin: Theme.spacingXS
                                        Layout.preferredHeight: Theme.spacingS
                                        radius: height / 2; color: Theme.selected
                                        Rectangle {
                                            width: parent.width * Math.max(0, Math.min(100, row.n.progress)) / 100
                                            height: parent.height; radius: parent.radius; color: Theme.accent
                                        }
                                    }
                                    // The application's actions, but the default one a click runs.
                                    Flow {
                                        visible: row.n.actions.length > 0
                                        Layout.fillWidth: true; Layout.topMargin: Theme.spacingXS
                                        spacing: Theme.spacingS
                                        Repeater {
                                            model: row.n.actions
                                            PushButton {
                                                required property var modelData
                                                objectName: "notificationHistoryAction"
                                                small: true
                                                text: modelData.label
                                                onClicked: history.center.invoke(row.n.notificationId, modelData.key)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // None between the macOS style's cards, which have no card under them to fade into.
            EdgeFade {
                objectName: "notificationFadeTop"
                anchors.top: parent.top
                fromTop: true
                shown: !Theme.macos && !list.atYBeginning
            }
            EdgeFade {
                objectName: "notificationFadeBottom"
                anchors.bottom: parent.bottom
                shown: !Theme.macos && !list.atYEnd
            }
        }
    }
}
