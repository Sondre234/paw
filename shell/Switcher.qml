// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The window switcher: every window's icon and title in a grid, most recently focused first,
// with the selected one's full title and place below, on a card with room around it for its
// shadow. A click picks a window. With shell.thumbnails each window is a card with its picture
// instead, as on Windows 11 (SwitcherCards.qml). In the macOS style the icons are large, without
// their titles, on a rounded translucent card, as macOS switches applications.
Item {
    id: switcher
    required property size screenSize
    // The output it is on, whose switcher it shows; "" for any.
    property string outputName: ""
    // The compositor's switcher, unless set (as a preview sets them), and the task model the
    // windows' pictures come from.
    property var windows: outputName === "" || shell.switcherOutput === outputName ? shell.switcherWindows : []
    property int selected: shell.switcherSelected
    property var taskSource: shell.tasks
    // Whether the windows are cards with their pictures: with shell.thumbnails, but for the macOS
    // style, which switches applications by their icons as macOS does.
    readonly property bool pictured: shell.thumbnails && !Theme.macos
    // Whether the switcher is open here, while its windows' pictures are wanted: from the moment
    // the compositor lists them, before it shows, so that they are there by then.
    readonly property bool open: windows.length > 0
    // What it lists, held as it was while it goes: the compositor forgets the windows as the
    // switcher closes, before the view hears that it should go, so an empty list is not taken.
    property var listed: []
    property int listedSelected: 0
    Component.onCompleted: { listed = windows; listedSelected = selected }
    onWindowsChanged: if (windows.length > 0) listed = windows
    onSelectedChanged: if (windows.length > 0) listedSelected = selected
    // A window's cell: its icon over two lines of its title.
    readonly property int cell: Theme.macos ? Theme.switcherIconSize + 3 * Theme.spacingL
                                            : Theme.appIconSizeDisplay + 10 * Theme.spacingM
    readonly property int padding: Theme.spacingXL
    // Most of the output's width, and most of its height but for the caption: as many columns
    // as fit across, and rows up to that height; the grid scrolls to the selection past that.
    readonly property real roomWidth: screenSize.width * 0.9 - 2 * padding
    readonly property real roomHeight: screenSize.height * 0.8 - 2 * padding - 48
    readonly property int columns: Math.max(1, Math.min(listed.length, Math.floor(roomWidth / cell)))
    readonly property int rows: Math.max(1, Math.min(Math.ceil(listed.length / columns), Math.floor(roomHeight / cell)))
    // What the grid, or the cards, take of the card.
    readonly property real contentWidth: pictured ? (cards.item ? cards.item.implicitWidth : 0) : columns * cell
    readonly property real contentHeight: pictured ? (cards.item ? cards.item.implicitHeight : 0) : rows * cell
    readonly property var current: listed[listedSelected] || ({})
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the card's opacity and growth. It comes in on the theme's
    // normal duration and goes on its fast one, quicker than it came, as the switch it ends is
    // done.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: switcher.shown }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        opacity: switcher.progress
        scale: 0.94 + 0.06 * switcher.progress
        width: switcher.contentWidth + 2 * switcher.padding
        height: switcher.contentHeight + 2 * switcher.padding + caption.height + Theme.spacingM
        CardFrame { radius: Theme.switcherRadius; color: Theme.switcherSurface }
        GridView {
            id: grid
            objectName: "switcherGrid"
            visible: !switcher.pictured
            x: switcher.padding; y: switcher.padding
            width: switcher.columns * switcher.cell; height: switcher.rows * switcher.cell
            cellWidth: switcher.cell; cellHeight: switcher.cell
            interactive: false
            clip: true
            model: switcher.pictured ? [] : switcher.listed
            currentIndex: switcher.listedSelected
            // The selection glides from window to window, once the switcher has come in.
            highlightMoveDuration: switcher.progress >= 1 ? Theme.durationNormal : 0
            highlight: Item {
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.spacingXS
                    radius: Theme.macos ? Theme.radiusLarge : Theme.radiusMedium
                    color: Theme.switcherSelection
                    border.color: Theme.macos ? "transparent" : Theme.accent; border.width: 2
                }
            }
            delegate: Item {
                id: entry
                required property var modelData
                required property int index
                width: switcher.cell; height: switcher.cell
                readonly property bool minimized: modelData.minimized === true
                readonly property bool urgent: modelData.urgent === true
                // A window asking for attention is tinted in the urgent colour, but for the
                // selection's own.
                Rectangle {
                    anchors.fill: parent; anchors.margins: Theme.spacingXS
                    radius: Theme.radiusMedium
                    color: entry.urgent && !entry.GridView.isCurrentItem ? Theme.urgentSubtle : "transparent"
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: pick.containsMouse ? Theme.hover : "transparent"
                    }
                }
                Image {
                    id: icon
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.macos ? (parent.height - height) / 2 : Theme.spacingXL
                    width: Theme.switcherIconSize; height: width
                    sourceSize: Qt.size(2 * width, 2 * height)
                    source: "image://icons/" + shell.iconFor(entry.modelData.appId)
                    // A minimized window's is faded.
                    opacity: entry.minimized ? 0.45 : 1
                }
                Text {
                    visible: !Theme.macos
                    anchors.top: icon.bottom; anchors.topMargin: Theme.spacingM + Theme.spacingXS
                    anchors.left: parent.left; anchors.right: parent.right; anchors.margins: Theme.spacingM
                    text: entry.modelData.title.length > 0 ? entry.modelData.title : entry.modelData.appId
                    textFormat: Text.PlainText
                    color: entry.minimized ? Theme.textMuted : Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                }
                // Badges on the icon's corners: a dot in the urgent colour for a window asking for
                // attention, a dash for a minimized one.
                Rectangle {
                    objectName: "switcherUrgent"
                    visible: entry.urgent
                    x: icon.x + icon.width - width + Theme.spacingXS; y: icon.y - Theme.spacingXS
                    width: Theme.iconSizeSmall; height: width; radius: width / 2
                    color: Theme.urgent; border.width: 2; border.color: Theme.surface
                }
                Rectangle {
                    objectName: "switcherMinimized"
                    visible: entry.minimized
                    x: icon.x + icon.width - width + Theme.spacingXS
                    y: icon.y + icon.height - height + Theme.spacingXS
                    width: Theme.iconSize + Theme.spacingS; height: width; radius: width / 2
                    color: Theme.surfaceRaised; border.color: Theme.border
                    Icon {
                        anchors.centerIn: parent
                        name: "minus"; size: Theme.iconSizeSmall; color: Theme.textMuted
                    }
                }
                MouseArea {
                    id: pick
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shell.switcherPick(entry.index)
                }
            }
        }
        Loader {
            id: cards
            x: switcher.padding; y: switcher.padding
            active: switcher.pictured
            sourceComponent: SwitcherCards {
                windows: switcher.listed
                selected: switcher.listedSelected
                taskSource: switcher.taskSource
                watching: switcher.open
                maxWidth: switcher.roomWidth
                maxHeight: switcher.roomHeight
                animate: switcher.progress >= 1
            }
        }
        // A label on a small pill, for what the caption says of a window's state.
        component Tag: Rectangle {
            property alias text: label.text
            implicitWidth: label.implicitWidth + 2 * Theme.spacingM
            implicitHeight: label.implicitHeight + Theme.spacingXS
            radius: height / 2
            color: Theme.selected
            Text {
                id: label
                anchors.centerIn: parent
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.Medium
            }
        }
        // The selected window's full title, and where it is: its workspace, by name too when it
        // has one, and its output; then whether it is minimized or asking for attention.
        Column {
            id: caption
            y: switcher.padding + switcher.contentHeight + Theme.spacingM
            x: switcher.padding; width: card.width - 2 * switcher.padding
            spacing: Theme.spacingS
            Text {
                width: parent.width
                text: switcher.current.title || switcher.current.appId || ""
                textFormat: Text.PlainText
                color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!switcher.current.output
                spacing: Theme.spacingS
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    readonly property string name: shell.workspaceNames[switcher.current.workspace - 1] || ""
                    text: "Workspace " + switcher.current.workspace + (name.length > 0 ? " · " + name : "")
                          + " · " + switcher.current.output
                    textFormat: Text.PlainText
                    color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                }
                Tag {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: switcher.current.minimized === true
                    text: "Minimized"
                }
                Tag {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: switcher.current.urgent === true
                    text: "Needs attention"
                    color: Theme.urgentSubtle
                }
            }
        }
    }
}
