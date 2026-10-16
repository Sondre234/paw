// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The sheet of keyboard shortcuts (shell.shortcuts; the shortcuts action, Super + /, and the
// command palette's Keyboard shortcuts): every key and button binding in effect, in groups by what
// they do, the binding mode's first while one is in use, each its keys as caps beside what it
// does, in columns on a card over the dimmed output. Super's cap shows the Windows logo; the macOS
// style writes the modifiers as a Mac does (⌃ ⌥ ⇧ ⌘), and the footer says which is which. Typing
// searches the titles, the keys and the groups; Up and Down, Page Up and Page Down, and Home and
// End while nothing is typed scroll; Escape, the action again or a click beside the card closes
// it. The card comes in each time its window shows, as the palette's does.
Item {
    id: root
    objectName: "shortcuts"
    readonly property var sheet: shell.shortcuts
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the scrim's and the card's opacity, and what is left of the
    // card's growth. It goes quicker than it came.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }

    function reset() {
        search.text = sheet.query
        tallest = contentHeight
        body.contentY = 0
        search.forceActiveFocus()
    }
    Connections {
        target: root.sheet
        function onQueryChanged() { if (search.text !== root.sheet.query) search.text = root.sheet.query }
    }

    readonly property int padding: Theme.spacingXXL
    // Room around the card, enough to keep it clear of the bars at either edge (the dock and its
    // headroom included), which dim under it.
    readonly property int marginX: Math.max(48, Math.round(width * 0.05))
    readonly property int marginY: Math.max(72, Math.round(height * 0.07))
    readonly property int rowHeight: Theme.macos ? 28 : 30
    // A button binding's second line, saying where it acts.
    readonly property int detailHeight: Theme.fontSizeCaption + Theme.spacingS
    readonly property int headingHeight: Theme.macos ? 26 : 32
    readonly property int groupGap: Theme.spacingL
    readonly property int columnGap: 2 * Theme.spacingXL
    readonly property int narrowest: 380
    readonly property int capHeight: Theme.macos ? 20 : 22
    // A key cap's face, the edge under it and its outline: solid, as keys are, on the glass too.
    readonly property color capFace: Theme.macos ? Theme.mix(Theme.popupSurface, Theme.text, Theme.light ? 0.07 : 0.12)
                                                 : Theme.mix(Theme.popupSurface, Theme.text, Theme.light ? 0.03 : 0.1)
    readonly property color capEdge: Theme.light ? Theme.alpha(Theme.text, 0.2) : Qt.rgba(0, 0, 0, 0.5)
    readonly property color capOutline: Theme.alpha(Theme.text, Theme.macos ? 0.08 : 0.14)

    function rowHeightOf(row) { return rowHeight + (row.detail !== "" ? detailHeight : 0) }
    function groupHeight(group) {
        var height = headingHeight
        for (var i = 0; i < group.rows.length; ++i)
            height += rowHeightOf(group.rows[i])
        return height
    }
    readonly property int columnCount: Math.max(1, Math.min(4, Math.floor((body.width + columnGap) / (narrowest + columnGap))))
    readonly property real columnWidth: Math.floor((body.width - (columnCount - 1) * columnGap) / columnCount)
    // The groups in columns, each put in the shortest so far, as {groups, height}.
    readonly property var columns: {
        var groups = sheet.groups
        var list = []
        for (var i = 0; i < columnCount; ++i)
            list.push({ groups: [], height: 0 })
        for (var g = 0; g < groups.length; ++g) {
            var shortest = 0
            for (var c = 1; c < columnCount; ++c)
                if (list[c].height < list[shortest].height)
                    shortest = c
            list[shortest].height += (list[shortest].groups.length > 0 ? groupGap : 0) + groupHeight(groups[g])
            list[shortest].groups.push(groups[g])
        }
        return list
    }
    readonly property real contentHeight: {
        var most = 0
        for (var i = 0; i < columns.length; ++i)
            most = Math.max(most, columns[i].height)
        return most
    }
    // The card keeps the height it opened at while a search narrows the sheet, so that it does
    // not jump about as one types.
    property real tallest: 0
    onContentHeightChanged: tallest = Math.max(tallest, contentHeight)

    function scroll(by) {
        body.contentY = Math.max(0, Math.min(body.contentHeight - body.height, body.contentY + by))
    }
    // The keys the search field leaves.
    function key(event) {
        var page = Math.max(rowHeight, body.height - 2 * rowHeight)
        switch (event.key) {
        case Qt.Key_Escape: sheet.close(); break
        case Qt.Key_Down: scroll(2 * rowHeight); break
        case Qt.Key_Up: scroll(-2 * rowHeight); break
        case Qt.Key_PageDown: scroll(page); break
        case Qt.Key_PageUp: scroll(-page); break
        case Qt.Key_Home:
        case Qt.Key_End:
            if (search.text !== "") {
                event.accepted = false
                return
            }
            body.contentY = event.key === Qt.Key_Home ? 0 : Math.max(0, body.contentHeight - body.height)
            break
        case Qt.Key_Tab:
        case Qt.Key_Backtab:
            break // the keyboard stays in the search
        default:
            event.accepted = false
            return
        }
        event.accepted = true
    }

    // The output dims under the card; a click beside the card closes it.
    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.progress
    }
    MouseArea {
        objectName: "shortcutsBackdrop"
        anchors.fill: parent
        onClicked: root.sheet.close()
    }

    // A key's cap: its name (`label`), Super's Windows logo (`logo`), or a Mac's modifier sign.
    component Cap: Item {
        id: cap
        property string label
        property bool logo: false
        implicitHeight: root.capHeight
        implicitWidth: logo ? root.capHeight + Theme.spacingS
                            : Math.max(root.capHeight, name.implicitWidth + 2 * Theme.spacingS + (Theme.macos ? 0 : 2))
        // The edge under the face, as a key stands up from the board.
        Rectangle {
            visible: !Theme.macos
            anchors.fill: parent; anchors.topMargin: 2
            radius: Theme.radiusSmall - 1
            color: root.capEdge
        }
        Rectangle {
            anchors.fill: parent; anchors.bottomMargin: Theme.macos ? 0 : 2
            radius: Theme.macos ? 4 : Theme.radiusSmall - 1
            color: root.capFace
            border.color: root.capOutline
            Text {
                id: name
                visible: !cap.logo
                anchors.centerIn: parent
                text: cap.label; textFormat: Text.PlainText
                color: Theme.text
                font.family: Theme.fontFamily; font.weight: Font.Medium
                font.pixelSize: cap.label.length === 1 ? Theme.fontSize : Theme.fontSizeSmall
            }
            // The four panes of the Windows logo.
            Grid {
                visible: cap.logo
                anchors.centerIn: parent
                columns: 2; spacing: 1
                Repeater {
                    model: 4
                    Rectangle { width: 5; height: 5; radius: 1; color: Theme.text }
                }
            }
        }
    }
    // What the macOS style writes each modifier as.
    readonly property var signs: ({ Super: "⌘", Ctrl: "⌃", Alt: "⌥", Shift: "⇧" })
    // A binding's keys: its modifiers, then its key (or the keys alike bindings differ by, or the
    // first and last of a run), as caps; joined by + in the taskbar style, side by side as a Mac
    // writes them in the macOS style.
    component KeyCaps: Row {
        id: keys
        property var mods: []
        property var names: []
        property bool range: false
        property bool button: false
        spacing: Theme.macos ? 3 : Theme.spacingS
        Repeater {
            model: keys.mods
            Row {
                required property string modelData
                spacing: keys.spacing
                Cap {
                    label: Theme.macos ? root.signs[modelData] || modelData : modelData
                    logo: !Theme.macos && modelData === "Super"
                }
                Text {
                    visible: !Theme.macos
                    anchors.verticalCenter: parent.verticalCenter
                    text: "+"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
            }
        }
        Repeater {
            model: keys.names
            Row {
                required property string modelData
                required property int index
                spacing: keys.spacing
                Text {
                    visible: keys.range && index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "–"
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
                // A mouse button's has a mouse before it, drawn rather than an Icon, whose shape
                // the software renderer would not clip to the scrolling sheet.
                Item {
                    visible: keys.button && index === 0 && modelData.indexOf("Lid") < 0 &&
                             modelData.indexOf("Tablet") < 0
                    width: 12; height: root.capHeight
                    Rectangle {
                        anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                        width: 10; height: 14; radius: 5
                        color: "transparent"
                        border.color: Theme.textMuted; border.width: 1.5
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 3; width: 1.5; height: 3.5; radius: 0.75
                            color: Theme.textMuted
                        }
                    }
                }
                Cap { label: modelData }
            }
        }
    }

    Item {
        id: card
        objectName: "shortcutsCard"
        anchors.centerIn: parent
        width: Math.min(root.width - 2 * root.marginX, 1680)
        height: Math.min(root.height - 2 * root.marginY,
                         body.y + Math.max(root.tallest, empty.implicitHeight) + root.padding + footer.height)
        opacity: root.progress
        scale: 0.96 + 0.04 * root.progress
        CardFrame { radius: Theme.spotlightRadius; innerEdge: true }
        // A click on the card itself closes nothing.
        MouseArea { anchors.fill: parent }

        // A keyboard on a disc in the accent, the title and how many shortcuts show; the search
        // at the end.
        Item {
            id: header
            x: root.padding; y: root.padding
            width: card.width - 2 * root.padding
            height: Math.max(badge.height, search.height)
            Rectangle {
                id: badge
                anchors.verticalCenter: parent.verticalCenter
                width: 40; height: 40
                radius: Theme.macos ? 10 : width / 2
                color: Theme.accentSubtle
                Icon { anchors.centerIn: parent; name: "keyboard"; size: Theme.iconSizeLarge; color: Theme.accent }
            }
            Column {
                anchors.left: badge.right; anchors.leftMargin: Theme.spacingL
                anchors.right: search.left; anchors.rightMargin: Theme.spacingL
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXS
                Text {
                    width: parent.width
                    text: "Keyboard shortcuts"
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                }
                Text {
                    objectName: "shortcutsSummary"
                    width: parent.width
                    elide: Text.ElideRight
                    text: root.sheet.query.trim() !== ""
                          ? root.sheet.count + " of " + root.sheet.total + " shortcuts"
                          : root.sheet.mode !== ""
                            ? "The " + root.sheet.mode + " mode is in use: its keys come first"
                            : root.sheet.total + (root.sheet.total === 1 ? " shortcut" : " shortcuts") + " in effect"
                    textFormat: Text.PlainText
                    color: Theme.textMuted
                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                }
            }
            SearchInput {
                id: search
                objectName: "shortcutsSearch"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                width: Math.min(320, header.width * 0.4)
                placeholderText: "Search shortcuts"
                onTextChanged: {
                    root.sheet.query = text
                    body.contentY = 0
                }
                Keys.onPressed: function(event) { root.key(event) }
            }
        }
        Rectangle {
            y: header.y + header.height + root.padding - 1
            width: card.width; height: 1
            color: Theme.divider
        }

        Flickable {
            id: body
            objectName: "shortcutsBody"
            x: root.padding; y: header.y + header.height + root.padding
            width: card.width - 2 * root.padding
            height: card.height - y - footer.height
            clip: true
            contentWidth: width
            contentHeight: root.contentHeight + 2 * root.padding
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; rightPadding: -root.padding + Theme.spacingS }
            Row {
                y: root.padding - Theme.spacingS
                spacing: root.columnGap
                Repeater {
                    model: root.columns
                    Column {
                        required property var modelData
                        width: root.columnWidth
                        spacing: root.groupGap
                        Repeater {
                            model: modelData.groups
                            delegate: group
                        }
                    }
                }
            }
        }
        // Nothing found, or nothing bound.
        EmptyState {
            id: empty
            objectName: "shortcutsEmpty"
            visible: root.sheet.count === 0
            x: root.padding; y: body.y + root.padding
            width: body.width
            icon: root.sheet.total === 0 ? "keyboard" : "search"
            title: root.sheet.total === 0 ? "No shortcuts are bound" : "No shortcut matches"
            hint: root.sheet.total === 0 ? "Bindings in the configuration's bindings list show here"
                                         : "Search by what a shortcut does, by a key such as Print or Super, or by a group such as Workspaces"
        }

        // Which cap is which modifier, and how to close the sheet.
        Item {
            id: footer
            anchors.bottom: parent.bottom
            width: card.width; height: 44
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
            Row {
                x: root.padding; anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXL
                Repeater {
                    model: Theme.macos ? ["Super", "Ctrl", "Alt", "Shift"] : ["Super"]
                    Row {
                        required property string modelData
                        spacing: Theme.spacingS
                        Cap {
                            label: Theme.macos ? root.signs[modelData] : modelData
                            logo: !Theme.macos
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData === "Super" ? (Theme.macos ? "Super" : "Super, the Windows key") : modelData
                            color: Theme.textMuted
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
            Text {
                anchors.right: parent.right; anchors.rightMargin: root.padding
                anchors.verticalCenter: parent.verticalCenter
                text: "Type to search · Esc to close"
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
            }
        }
    }

    // A group: its heading (with a dot in the accent for the binding mode's), then its rows.
    Component {
        id: group
        Column {
            id: section
            required property var modelData
            objectName: "shortcutsGroup:" + modelData.name
            width: root.columnWidth
            Row {
                height: root.headingHeight
                spacing: Theme.spacingS
                Rectangle {
                    visible: section.modelData.mode
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8; height: 8; radius: 4
                    color: Theme.accent
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: section.modelData.name; textFormat: Text.PlainText
                    color: section.modelData.mode ? Theme.accent : Theme.macos ? Theme.textMuted : Theme.text
                    font.family: Theme.fontFamily; font.weight: Font.DemiBold
                    font.pixelSize: Theme.macos ? Theme.fontSizeSmall : Theme.fontSizeLarge
                }
            }
            Repeater {
                model: section.modelData.rows
                Item {
                    id: line
                    required property var modelData
                    objectName: "shortcut:" + modelData.title
                    width: root.columnWidth
                    height: root.rowHeightOf(modelData)
                    HoverHandler { id: hover }
                    Rectangle {
                        anchors.fill: parent
                        anchors.leftMargin: -Theme.spacingS; anchors.rightMargin: -Theme.spacingS
                        radius: Theme.radiusSmall
                        color: hover.hovered ? Theme.hover : "transparent"
                    }
                    Column {
                        anchors.left: parent.left; anchors.right: keys.left; anchors.rightMargin: Theme.spacingL
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            width: parent.width
                            text: line.modelData.title; textFormat: Text.PlainText
                            elide: Text.ElideRight; maximumLineCount: 1
                            color: Theme.text
                            font.family: line.modelData.command ? "monospace" : Theme.fontFamily
                            font.pixelSize: line.modelData.command ? Theme.fontSizeSmall : Theme.fontSize
                        }
                        Text {
                            visible: line.modelData.detail !== ""
                            width: parent.width
                            text: line.modelData.detail; textFormat: Text.PlainText
                            elide: Text.ElideRight
                            color: Theme.textMuted
                            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption
                        }
                    }
                    KeyCaps {
                        id: keys
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        mods: line.modelData.mods
                        names: line.modelData.keys
                        range: line.modelData.range
                        button: line.modelData.button
                    }
                }
            }
        }
    }
}
