// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The text of the overview. The compositor draws the thumbnails, the workspace strip and the
// selection; this sits over them in the output's coordinates with each window's title, the
// strip's workspace labels, the search box and a hint. It takes no input: the compositor keeps
// the keyboard and the pointer while the overview is open. Snap Assist is the overview in the
// free slot beside a window just snapped (`area`): titles and a hint there, no search.
Item {
    id: overview
    required property size screenSize
    // The compositor's overview, unless set (as a preview sets them).
    property var windows: shell.overviewWindows
    property var strip: shell.overviewStrip
    property string filter: shell.overviewFilter
    property rect area: shell.overviewArea
    property bool assist: shell.overviewAssist
    // The search box's text, held as it was while it goes: the controller forgets it as the
    // overview closes, before the view hears that it should go, so only an open overview's is
    // taken, and it starts afresh each time it shows.
    property string shownFilter: ""
    Component.onCompleted: shownFilter = filter
    onShownChanged: if (shown) shownFilter = filter
    onFilterChanged: if (shell.overviewOutput !== "") shownFilter = filter
    property int selected: shell.overviewSelected
    property int viewed: shell.overviewViewed
    // The workspaces with a window asking for attention.
    property var urgentWorkspaces: (shell.workspaces[shell.overviewOutput] || ({})).urgent || []
    width: screenSize.width
    height: screenSize.height
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1, its opacity: it fades in as the compositor's thumbnails
    // glide to their places. As they glide back the titles and the strip's labels go with them at
    // once, and the search box and the hint fade out.
    property bool shown: false
    property real progress: 0
    opacity: progress
    states: State {
        name: "shown"
        when: overview.shown
        PropertyChanges { overview.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationSlow; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]

    // The search box, drawn as the palette's and the start menu's fields are (it takes no input):
    // what was typed, or what typing does. It keeps to the room the compositor leaves above the
    // strip.
    Rectangle {
        id: search
        visible: !overview.assist
        anchors.horizontalCenter: parent.horizontalCenter
        y: overview.area.y + Theme.spacingS
        width: Math.min(460, overview.width - 2 * Theme.spacingXL); height: Theme.rowHeight
        radius: height / 2
        color: Theme.surface
        border.color: overview.shownFilter.length > 0 ? Theme.accent : Theme.border
        Icon {
            id: magnifier
            x: Theme.spacingL; anchors.verticalCenter: parent.verticalCenter
            name: "search"; size: Theme.iconSize
            color: overview.shownFilter.length > 0 ? Theme.text : Theme.textMuted
        }
        Text {
            anchors.fill: parent
            anchors.leftMargin: magnifier.x + magnifier.width + Theme.spacingM
            anchors.rightMargin: Theme.spacingL
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
            text: overview.shownFilter.length > 0 ? overview.shownFilter : qsTr("Type to search windows")
            color: overview.shownFilter.length > 0 ? Theme.text : Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
        }
    }

    // Workspace labels over the strip's cells: the number or name, and how many windows.
    Repeater {
        model: overview.strip
        delegate: Item {
            id: cell
            required property var modelData
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            readonly property string name: shell.workspaceNames[modelData.workspace - 1] || ""
            // A window on this workspace is asking for attention.
            readonly property bool urgent: overview.urgentWorkspaces.indexOf(modelData.workspace) >= 0
            // A pill in the cell's corner: in the accent colour for the workspace shown, ringed in
            // the urgent colour for one with a window asking for attention.
            Rectangle {
                anchors.left: parent.left; anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingS
                width: label.implicitWidth + 2 * Theme.spacingM
                height: label.implicitHeight + 2 * Theme.spacingXS
                radius: height / 2
                color: modelData.workspace === overview.viewed ? Theme.accent : Theme.surface
                border.width: cell.urgent ? 2 : 0; border.color: Theme.urgent
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: cell.name.length > 0 ? modelData.workspace + " " + cell.name : modelData.workspace
                    color: modelData.workspace === overview.viewed ? Theme.textOnAccent : Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold
                }
            }
        }
    }

    // Each window's title on a bar along the bottom of its thumbnail; the selected one's in full.
    Repeater {
        model: overview.windows
        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            readonly property bool selected: index === overview.selected
            readonly property string title: modelData.title.length > 0 ? modelData.title : modelData.appId
            visible: width >= 48
            // A window asking for attention has a frame in the urgent colour.
            Rectangle {
                objectName: "overviewUrgent"
                anchors.fill: parent
                visible: entry.modelData.urgent === true
                color: "transparent"; border.width: 3; border.color: Theme.urgent
            }
            Rectangle {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: Theme.iconSizeSmall + 2 * Theme.spacingS
                color: Theme.alpha(Theme.surface, entry.selected ? 0.94 : 0.82)
                Image {
                    id: icon
                    x: Theme.spacingS; anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSizeSmall; height: Theme.iconSizeSmall
                    sourceSize: Qt.size(2 * Theme.iconSizeSmall, 2 * Theme.iconSizeSmall)
                    source: "image://icons/" + shell.iconFor(entry.modelData.appId)
                }
                Text {
                    anchors.left: icon.right; anchors.leftMargin: Theme.spacingS + Theme.spacingXS
                    anchors.right: parent.right; anchors.rightMargin: Theme.spacingS + Theme.spacingXS
                    anchors.verticalCenter: parent.verticalCenter
                    text: entry.title; textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    font.weight: entry.selected ? Font.DemiBold : Font.Normal
                }
            }
        }
    }

    // What is said over the compositor's backdrop, which is dark whatever the theme, stands on a
    // pill of the theme's surface.
    Rectangle {
        anchors.centerIn: parent
        visible: overview.shown && overview.windows.length === 0 && !overview.assist
        width: empty.implicitWidth + 2 * Theme.spacingXL
        height: empty.implicitHeight + 2 * Theme.spacingM
        radius: height / 2
        color: Theme.surface
        border.color: Theme.border
        Text {
            id: empty
            anchors.centerIn: parent
            text: overview.filter.length > 0 ? qsTr("No window matches") : qsTr("No windows here")
            color: Theme.text
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle
        }
    }

    // How to work it, along the bottom in the gap below the thumbnails (of Snap Assist's slot):
    // each key or gesture, then what it does.
    Rectangle {
        x: overview.assist ? overview.area.x + (overview.area.width - width) / 2
                           : (overview.width - width) / 2
        y: overview.area.y + overview.area.height - height - Theme.spacingXS
        width: hints.implicitWidth + 2 * Theme.spacingL
        height: hints.implicitHeight + 2 * Theme.spacingXS
        radius: height / 2
        color: Theme.surface
        Row {
            id: hints
            anchors.centerIn: parent
            spacing: Theme.spacingL
            Repeater {
                model: overview.assist
                       ? [{ "key": qsTr("Enter"), "does": qsTr("puts the window here") },
                          { "key": qsTr("Esc"), "does": qsTr("leaves it empty") }]
                       : [{ "key": qsTr("Enter"), "does": qsTr("picks") },
                          { "key": qsTr("Esc"), "does": qsTr("closes") },
                          { "key": qsTr("Middle click"), "does": qsTr("closes a window") },
                          { "key": qsTr("Drag"), "does": qsTr("a window onto a workspace to move it") }]
                delegate: Text {
                    required property var modelData
                    text: "<b>" + modelData.key + "</b> " + modelData.does
                    textFormat: Text.StyledText
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
    }
}
