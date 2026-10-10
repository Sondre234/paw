// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// The command palette: a search box over windows, applications, workspaces, actions and saved
// sessions, with a calculator and files. Up and Down (or Ctrl+N and Ctrl+P) select, Enter runs,
// Ctrl+Enter opens a file's folder, Escape closes; a leading > @ # or % narrows the search to
// actions, windows, workspaces or sessions, = to the calculator and / to files. It is a card
// with room around it for its shadow, coming in each time its window shows.
//
// In the macOS style it is Spotlight: a large field along the card's top, and under a line the
// results in groups under small headings (the first, the top hit), the one chosen filled with the
// accent.
Item {
    id: root
    required property size screenSize
    readonly property int padding: Theme.spacingL
    // A result's row: a title over a subtitle.
    readonly property int rowHeight: Theme.rowHeight + Theme.spacingL
    readonly property int visibleRows: Math.max(1, Math.min(8, Math.floor((screenSize.height * 0.6 - 3 * padding - input.height) / rowHeight)))
    readonly property var kindLabels: ({ window: "Window", app: "App", workspace: "Workspace", action: "Action", session: "Session",
                                         calc: "Calculator", file: "File", web: "Web" })
    // Spotlight's headings: the top hit's over the first result of a search, and a kind's over the
    // first of a run of it.
    readonly property var groupLabels: ({ window: "Windows", app: "Applications", workspace: "Workspaces",
                                          action: "Actions", session: "Sessions", calc: "Calculator",
                                          file: "Files", web: "Web" })
    // The results, read from the palette once each time they change.
    readonly property var results: shell.palette.results
    function heading(index) {
        var results = root.results
        // A row the results no longer have, read again before it goes, has none.
        if (index >= results.length)
            return ""
        var searching = shell.palette.query.trim() !== ""
        if (index === 0)
            return searching ? "Top Hit" : groupLabels[results[0].kind] || ""
        return results[index].kind !== results[index - 1].kind || (index === 1 && searching)
            ? groupLabels[results[index].kind] || "" : ""
    }
    // How tall Spotlight's results are, as far as most of the output's height leaves room for.
    readonly property real spotlightHeight: {
        var results = root.results
        var most = screenSize.height * 0.6 - Theme.spotlightFieldHeight - 2 * Theme.spacingM
        var sum = 0
        for (var i = 0; i < results.length && sum < most; ++i)
            sum += Theme.spotlightRowHeight + (heading(i) !== "" ? Theme.spotlightHeadingHeight : 0)
        return Math.min(sum, Math.max(Theme.spotlightRowHeight, most))
    }
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the card's opacity, and what is left of its drop and
    // growth. It goes quicker than it came, what it ran showing through.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }

    function reset() {
        input.text = shell.palette.query
        input.forceActiveFocus()
    }
    Connections {
        target: shell.palette
        function onQueryChanged() { if (input.text !== shell.palette.query) input.text = shell.palette.query }
    }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        width: Math.min(680, root.screenSize.width - 32)
        height: Theme.macos ? input.height + 1 + 2 * Theme.spacingM + (list.count > 0 ? list.height : empty.height)
                            : input.height + (list.count > 0 ? list.height : empty.height) + 3 * root.padding
        opacity: root.progress
        scale: 0.97 + 0.03 * root.progress
        transform: Translate { y: (root.progress - 1) * Theme.spacingM }
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.spotlightRadius
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.spotlightRadius
            color: Theme.popupSurface
            border.color: Theme.popupOutline
            Rectangle {
                visible: Theme.popupInnerEdge.a > 0
                anchors.fill: parent; anchors.margins: 1
                radius: parent.radius - 1
                color: "transparent"
                border.color: Theme.popupInnerEdge
            }
        }

        // The start menu's field, larger; Spotlight's along the card's top, a line under it.
        SearchInput {
            id: input
            objectName: "paletteInput"
            x: Theme.macos ? Theme.spacingXS : root.padding; y: Theme.macos ? 0 : root.padding
            width: card.width - 2 * x
            large: true
            placeholderText: Theme.macos ? "Spotlight Search" : "Search windows, apps, files, workspaces, actions, sessions"
            onTextChanged: shell.palette.query = text
            Keys.onPressed: function(event) {
                const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
                if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab || (ctrl && (event.key === Qt.Key_N || event.key === Qt.Key_J))) {
                    shell.palette.move(1); event.accepted = true
                } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Backtab || (ctrl && (event.key === Qt.Key_P || event.key === Qt.Key_K))) {
                    shell.palette.move(-1); event.accepted = true
                } else if (event.key === Qt.Key_PageDown) {
                    shell.palette.move(root.visibleRows); event.accepted = true
                } else if (event.key === Qt.Key_PageUp) {
                    shell.palette.move(-root.visibleRows); event.accepted = true
                } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                    if (ctrl) shell.palette.reveal()
                    else shell.palette.activate()
                    event.accepted = true
                } else if (event.key === Qt.Key_Escape) {
                    shell.palette.close(); event.accepted = true
                }
            }
        }

        Rectangle {
            visible: Theme.macos
            y: input.height; width: card.width; height: 1
            color: Theme.divider
        }

        ListView {
            id: list
            objectName: "paletteList"
            x: Theme.macos ? Theme.spacingM : root.padding
            y: input.y + input.height + (Theme.macos ? 1 + Theme.spacingM : root.padding)
            width: card.width - 2 * x
            height: Theme.macos ? root.spotlightHeight : Math.min(count, root.visibleRows) * root.rowHeight
            clip: true
            interactive: false
            model: shell.palette.results
            currentIndex: shell.palette.selected
            // The selection is shaded as a menu's row is, it being what Enter runs; it moves at
            // once, as fast as the keys go. Spotlight's row fills itself.
            highlightMoveDuration: 0
            highlight: Rectangle {
                visible: !Theme.macos
                radius: Theme.radiusSmall
                color: Theme.selected
            }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            delegate: Theme.macos ? spotlightRow : paletteRow
        }
        // A result: its icon, its title over what it is, and its kind on a pill at the end.
        Component {
            id: paletteRow
            Item {
                id: row
                required property var modelData
                required property int index
                width: list.width; height: root.rowHeight
                Image {
                    id: icon
                    x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
                    width: Theme.appIconSizeLarge; height: Theme.appIconSizeLarge
                    sourceSize: Qt.size(2 * Theme.appIconSizeLarge, 2 * Theme.appIconSizeLarge)
                    source: "image://icons/" + row.modelData.icon
                }
                // A window asking for attention has a dot in the urgent colour on its icon, as
                // in the switcher.
                Rectangle {
                    visible: row.modelData.urgent === true
                    x: icon.x + icon.width - width + Theme.spacingXS; y: icon.y - Theme.spacingXS
                    width: Theme.spacingL; height: width; radius: width / 2
                    color: Theme.urgent; border.width: 2; border.color: Theme.surface
                }
                Column {
                    anchors.left: icon.right; anchors.leftMargin: Theme.spacingL
                    anchors.right: folder.visible ? folder.left : kind.left; anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingXS
                    Text {
                        width: parent.width
                        text: row.modelData.title; textFormat: Text.PlainText
                        color: Theme.text; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.subtitle; textFormat: Text.PlainText
                        color: Theme.textMuted; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                }
                Rectangle {
                    id: kind
                    anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
                    anchors.verticalCenter: parent.verticalCenter
                    width: kindLabel.implicitWidth + 2 * Theme.spacingM
                    height: kindLabel.implicitHeight + 2 * Theme.spacingXS
                    radius: height / 2
                    color: "transparent"
                    border.color: Theme.border
                    Text {
                        id: kindLabel
                        anchors.centerIn: parent
                        text: row.modelData.folder === true ? "Folder" : root.kindLabels[row.modelData.kind] || ""
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.Medium
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: shell.palette.selected = row.index
                    onClicked: shell.palette.activate(row.index)
                }
                // A file's folder opens from a button beside its kind, as Ctrl+Enter opens it.
                FlatButton {
                    id: folder
                    objectName: "paletteFolder:" + row.modelData.title
                    visible: row.modelData.kind === "file"
                    anchors.right: kind.left; anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.rowHeight; height: Theme.rowHeight
                    focusPolicy: Qt.NoFocus
                    Accessible.name: "Open the folder"
                    onHoveredChanged: if (hovered) shell.palette.selected = row.index
                    onClicked: shell.palette.reveal(row.index)
                    contentItem: Item { Icon { anchors.centerIn: parent; name: "folder-open"; size: Theme.iconSizeSmall; color: Theme.textMuted } }
                }
            }
        }
        // Spotlight's result: its group's heading over the first of a group, then its icon, its
        // title and, muted at the end, what it is; filled with the accent while it is chosen.
        Component {
            id: spotlightRow
            Column {
                id: result
                required property var modelData
                required property int index
                readonly property bool chosen: index === shell.palette.selected
                readonly property string heading: root.heading(index)
                width: list.width
                Text {
                    visible: result.heading !== ""
                    width: parent.width; height: Theme.spotlightHeadingHeight
                    leftPadding: Theme.spacingM
                    verticalAlignment: Text.AlignVCenter
                    text: result.heading
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold
                }
                Rectangle {
                    width: parent.width; height: Theme.spotlightRowHeight
                    radius: Theme.radiusSmall
                    color: result.chosen ? Theme.accent : "transparent"
                    Image {
                        id: glyph
                        x: Theme.spacingS; anchors.verticalCenter: parent.verticalCenter
                        width: Theme.spotlightIconSize; height: width
                        sourceSize: Qt.size(2 * width, 2 * height)
                        source: "image://icons/" + result.modelData.icon
                    }
                    // A window asking for attention has a dot in the urgent colour on its icon.
                    Rectangle {
                        visible: result.modelData.urgent === true
                        x: glyph.x + glyph.width - width + Theme.spacingXS; y: glyph.y - Theme.spacingXS
                        width: Theme.spacingM + Theme.spacingXS; height: width; radius: width / 2
                        color: Theme.urgent
                        border.width: 2; border.color: result.chosen ? Theme.accent : Theme.popupSurface
                    }
                    Text {
                        anchors.left: glyph.right; anchors.leftMargin: Theme.spacingM
                        anchors.right: what.left; anchors.rightMargin: Theme.spacingL
                        anchors.verticalCenter: parent.verticalCenter
                        text: result.modelData.title; textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: result.chosen ? Theme.textOnAccentFill : Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeLarge
                    }
                    Text {
                        id: what
                        anchors.right: folder.visible ? folder.left : parent.right
                        anchors.rightMargin: folder.visible ? Theme.spacingS : Theme.spacingM
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width / 3)
                        text: result.modelData.subtitle; textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: result.chosen ? Theme.alpha(Theme.textOnAccentFill, 0.8) : Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                    }
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: shell.palette.selected = result.index
                        onClicked: shell.palette.activate(result.index)
                    }
                    // The chosen file's folder opens from a button at its end, as Ctrl+Enter
                    // opens it.
                    FlatButton {
                        id: folder
                        objectName: "paletteFolder:" + result.modelData.title
                        visible: result.chosen && result.modelData.kind === "file"
                        anchors.right: parent.right; anchors.rightMargin: Theme.spacingXS
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.height - 2 * Theme.spacingXS; height: width
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "Open the folder"
                        onClicked: shell.palette.reveal(result.index)
                        contentItem: Item { Icon { anchors.centerIn: parent; name: "folder-open"; size: Theme.iconSizeSmall; color: Theme.textOnAccentFill } }
                    }
                }
            }
        }
        // What to try when nothing matches: the prefixes that narrow a search.
        EmptyState {
            id: empty
            objectName: "paletteEmpty"
            visible: list.count === 0
            x: root.padding; y: list.y
            width: card.width - 2 * root.padding
            icon: "search"
            title: "Nothing matches"
            hint: "Start with > for actions, @ for windows, # for workspaces, % for sessions, / for files or = to calculate"
        }
    }
}
