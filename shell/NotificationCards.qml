// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The notification cards: a stack of them from a corner of the focused monitor, each sliding in
// and out. Clicking a card runs its default action or dismisses it; hovering one holds its timer,
// whose time left runs down as a line along the card's bottom edge, and shows its close button.
// In the macOS style they are banners: the application's icon beside the bold summary, the time at
// its end and the body under it, the close button on the card's corner, and no line.
Item {
    id: cards
    required property string outputName
    readonly property var center: shell.notifications
    // The gap between the cards and the screen's edges, and the room on their other sides, where
    // their shadows fall when there are any.
    readonly property int margin: Theme.spacingL
    readonly property int spread: Math.max(margin, Theme.shadowMargin)
    // The output the cards are on; the surface stays while the last ones slide away.
    readonly property bool targeted: shell.cardsOutput === outputName
    property int settled: 0
    readonly property int shown: list.contentHeight
    readonly property bool active: targeted || settled > 0
    onShownChanged: {
        if (shown >= settled) { settled = shown; shrink.stop() }
        else shrink.restart()
    }
    // The surface follows the cards' height up at once but down only after they have left, or a
    // card sliding out would be cut off.
    Timer { id: shrink; interval: Theme.durationSlow; onTriggered: cards.settled = cards.shown }
    // How long ago a card came, as the history says it, kept current while cards are up.
    property date now: new Date()
    Timer { interval: 30000; repeat: true; running: cards.active; onTriggered: cards.now = new Date() }
    onActiveChanged: now = new Date()
    function ago(time) {
        var seconds = (now.getTime() - time.getTime()) / 1000
        if (seconds < 60) return "now"
        if (seconds < 3600) return Math.floor(seconds / 60) + " min ago"
        return Qt.formatTime(time, "HH:mm")
    }
    width: center.cardWidth + margin + spread
    height: active ? Math.max(1, settled + margin + spread) : 1

    ListView {
        id: list
        objectName: "notificationCards"
        // Taller than any stack, so every card has a delegate and contentHeight is the stack's
        // height; the surface, not the list, bounds what is drawn. Cards start at the edge of
        // the corner they stack from.
        readonly property int room: 4000
        x: center.left ? cards.margin : cards.spread
        y: center.bottom ? cards.height - cards.margin - room : cards.margin
        width: center.cardWidth
        height: room
        interactive: false
        spacing: Theme.spacingL
        // Newest nearest the screen edge the stack starts from.
        verticalLayoutDirection: center.bottom ? ListView.BottomToTop : ListView.TopToBottom
        model: cards.active ? center.cards : null
        // A card slides in from the screen's edge, all the way past it, and back out to it when
        // it goes; the others make room for it or close up after it.
        readonly property real offscreen: (center.left ? -1 : 1) * (center.cardWidth + cards.margin)
        add: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationNormal; easing.type: Theme.easing }
                NumberAnimation { property: "x"; from: list.offscreen; duration: Theme.durationSlow; easing.type: Theme.easing }
            }
        }
        remove: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; to: 0; duration: Theme.durationNormal; easing.type: Theme.easingExit }
                NumberAnimation { property: "x"; to: list.offscreen; duration: Theme.durationNormal; easing.type: Theme.easingExit }
            }
        }
        displaced: Transition { NumberAnimation { properties: "y"; duration: Theme.durationNormal; easing.type: Theme.easing } }
        delegate: Item {
            id: entry
            required property int index
            required property int notificationId
            required property string app
            required property string icon
            required property string summary
            required property string body
            required property var actions
            required property bool hasDefault
            required property int urgency
            required property int progress
            required property bool hasImage
            required property string desktopEntry
            required property var time
            readonly property bool critical: urgency === 2
            // The installed application's icon, "" when it is not one.
            readonly property string appIcon: {
                var known = desktopEntry.length > 0 ? desktopEntry : app.toLowerCase()
                return shell.appFor(known).length > 0 ? shell.iconFor(known) : ""
            }
            // The header's icon: the application's, else the notification's own.
            readonly property string headerIcon: appIcon.length > 0 ? appIcon : icon
            // The picture beside the text: the notification's image (a contact's, an album's),
            // else its own icon when the header shows the application's instead; in the macOS
            // style, which has no header, else the header's icon.
            readonly property string picture: hasImage ? "image://notify/" + notificationId + "/" + Number(time)
                : icon.length > 0 && appIcon.length > 0 && icon !== appIcon ? "image://icons/" + icon
                : Theme.macos && headerIcon.length > 0 ? "image://icons/" + headerIcon : ""
            // How long it stays, from when it came or was last replaced (`time`), 0 for until it
            // is dismissed; and how much of that is left, from 1 to 0, run down as the daemon's
            // timer runs, held while the pointer holds that.
            readonly property int lifetime: {
                time // asked again when the notification is replaced
                return cards.center.cardLifetime(notificationId)
            }
            property real remaining: 1
            function countDown() {
                countdown.stop()
                remaining = 1
                if (lifetime > 0) {
                    countdown.start()
                    countdown.paused = hover.hovered
                }
            }
            onLifetimeChanged: countDown()
            onTimeChanged: countDown()
            Component.onCompleted: countDown()
            NumberAnimation {
                id: countdown
                target: entry; property: "remaining"
                from: 1; to: 0; duration: entry.lifetime
            }
            width: list.width
            height: card.height

            Item {
                id: card
                objectName: "notificationCard"
                width: parent.width
                height: content.implicitHeight + 2 * content.y
                // A critical notification, which stays until it is dismissed, is outlined in the
                // danger colour, with a band of it down its side.
                CardFrame {
                    color: entry.critical ? Theme.mix(Theme.popupSurface, Theme.dangerFill, 0.06) : Theme.popupSurface
                    outline: entry.critical ? Theme.danger : Theme.popupOutline
                    innerEdge: true
                }
                Item {
                    visible: entry.critical
                    width: Theme.spacingS; height: parent.height
                    clip: true
                    Rectangle {
                        width: 2 * Theme.radiusLarge; height: parent.height
                        radius: Theme.radiusLarge
                        color: Theme.danger
                    }
                }
                // The pointer anywhere on the card, its buttons too, holds its timer.
                HoverHandler {
                    id: hover
                    onHoveredChanged: {
                        cards.center.hold(entry.notificationId, hovered)
                        if (countdown.running) countdown.paused = hovered
                    }
                }
                Component.onDestruction: cards.center.hold(entry.notificationId, false)
                MouseArea {
                    anchors.fill: parent
                    onClicked: cards.center.activate(entry.notificationId)
                }
                // The macOS style's close button, on the card's top-left corner.
                CloseButton {
                    objectName: "notificationCornerClose"
                    visible: Theme.macos && opacity > 0
                    z: 1
                    x: -Theme.spacingS; y: -Theme.spacingS
                    size: Theme.iconSize + Theme.spacingXS
                    opacity: hover.hovered ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                    background: Rectangle {
                        radius: width / 2
                        color: Theme.popupSurface
                        border.color: Theme.popupOutline
                    }
                    Accessible.name: "Dismiss"
                    onClicked: cards.center.dismiss(entry.notificationId)
                }
                // The time it has left, a line along its bottom edge that shortens towards its
                // start; with animations off it is not drawn.
                Rectangle {
                    objectName: "notificationCountdown"
                    visible: entry.lifetime > 0 && Theme.animations && !Theme.macos
                    x: Theme.radiusLarge
                    y: parent.height - height - 1
                    width: (parent.width - 2 * Theme.radiusLarge) * entry.remaining
                    height: Theme.spacingXS
                    radius: height / 2
                    color: Theme.accent
                }
                ColumnLayout {
                    id: content
                    x: Theme.spacingL + Theme.spacingXS; y: x; width: parent.width - 2 * x
                    spacing: Theme.spacingM
                    // Who it is from and when: the application's icon and name, and the time.
                    RowLayout {
                        visible: !Theme.macos
                        Layout.fillWidth: true
                        spacing: Theme.spacingS + Theme.spacingXS
                        Image {
                            visible: entry.headerIcon.length > 0
                            Layout.preferredWidth: Theme.iconSizeSmall; Layout.preferredHeight: Theme.iconSizeSmall
                            sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                            fillMode: Image.PreserveAspectFit
                            source: entry.headerIcon.length > 0 ? "image://icons/" + entry.headerIcon : ""
                        }
                        Text {
                            Layout.fillWidth: true
                            text: entry.app.length > 0 ? entry.app + " · " + cards.ago(entry.time) : cards.ago(entry.time)
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                            textFormat: Text.PlainText; elide: Text.ElideRight
                        }
                        // Dismisses the card; it shows while the pointer is on the card.
                        CloseButton {
                            objectName: "notificationClose"
                            Layout.preferredWidth: size; Layout.preferredHeight: size
                            opacity: hover.hovered ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                            Accessible.name: "Dismiss"
                            onClicked: cards.center.dismiss(entry.notificationId)
                        }
                    }
                    // What it says, the summary over the body, beside its picture.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacingL
                        // Twice an application's icon.
                        Image {
                            readonly property real size: Theme.macos ? Theme.notificationIconSize : 2 * Theme.appIconSize
                            visible: entry.picture.length > 0
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: size; Layout.preferredHeight: size
                            sourceSize: Qt.size(2 * size, 2 * size)
                            fillMode: Image.PreserveAspectFit
                            source: entry.picture
                            cache: false
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            spacing: Theme.spacingXS
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Theme.spacingS
                                Text {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: entry.summary
                                    color: Theme.text
                                    font.pixelSize: Theme.macos ? Theme.fontSize : Theme.fontSizeLarge
                                    font.weight: Font.DemiBold; font.family: Theme.fontFamily
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                                }
                                // The macOS style's time, at the summary's end.
                                Text {
                                    visible: Theme.macos
                                    Layout.alignment: Qt.AlignTop
                                    text: cards.ago(entry.time)
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: text.length > 0
                                text: entry.body
                                color: Theme.macos ? Theme.text : Theme.textMuted
                                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                                lineHeight: 1.1
                                textFormat: Text.StyledText
                                linkColor: Theme.accent
                                wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                                onLinkActivated: (link) => cards.center.openLink(link)
                            }
                            // The application's own progress (a download, a copy).
                            Rectangle {
                                visible: entry.progress >= 0
                                Layout.fillWidth: true; Layout.topMargin: Theme.spacingS
                                Layout.preferredHeight: Theme.spacingS + Theme.spacingXS
                                radius: height / 2; color: Theme.selected
                                Rectangle {
                                    width: parent.width * Math.max(0, entry.progress) / 100; height: parent.height
                                    radius: height / 2; color: Theme.accent
                                }
                            }
                        }
                    }
                    // The application's actions, as buttons of one width across the card, up to
                    // three to a row.
                    GridLayout {
                        visible: entry.actions.length > 0
                        Layout.fillWidth: true; Layout.topMargin: Theme.spacingXS
                        columns: Math.min(3, entry.actions.length)
                        columnSpacing: Theme.spacingM; rowSpacing: Theme.spacingM
                        Repeater {
                            model: entry.actions
                            delegate: PushButton {
                                required property var modelData
                                objectName: "notificationAction"
                                text: modelData.label
                                Layout.fillWidth: true; Layout.preferredWidth: 1
                                onClicked: cards.center.invoke(entry.notificationId, modelData.key)
                            }
                        }
                    }
                }
            }
        }
    }
}
