// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

// The emoji picker (shell.emoji; the emoji_picker action, Super + semicolon), as Windows' Win + .
// shows it: a search field, a tab for the emoji picked lately and one for each of Unicode's groups,
// a grid of emoji, and the skin tone of those that take one. Typing searches by name and keyword;
// the arrows move through the grid (Left and Right while the search is empty, or at its ends),
// Tab and Shift + Tab too, Page Down and Page Up change tabs, Ctrl+T changes the skin tone, Enter
// or a click picks the emoji, which is typed into the window that had the keyboard, and Escape
// clears the search, then closes. A card like the command palette's, with room around it for its
// shadow, coming in each time its window shows.
Item {
    id: root
    required property size screenSize
    readonly property int padding: Theme.spacingL
    readonly property var picker: shell.emoji
    // A tab's glyph for each group, by its name.
    readonly property var glyphs: ({ "Smileys & Emotion": "😀", "People & Body": "👋", "Animals & Nature": "🐻",
                                     "Food & Drink": "🍔", "Travel & Places": "✈️", "Activities": "⚽",
                                     "Objects": "💡", "Symbols": "🔣", "Flags": "🏁" })
    // The tabs: the recent emoji while there are any, then the groups, as {name, glyph, group}
    // (-1 for the recent ones).
    readonly property var tabs: {
        var list = picker.recent.length > 0 ? [{ name: "Recently used", glyph: "🕘", group: -1 }] : []
        return list.concat(picker.groups.map(function(name, index) {
            return { name: name, glyph: glyphs[name] || "•", group: index }
        }))
    }
    property int tab: 0
    readonly property string query: search.text.trim()
    // What the grid shows: what the search finds, else the tab's emoji, as {text, name}.
    readonly property var shownEmoji: {
        picker.tone // in the tone chosen
        var chosen = tabs[Math.min(tab, tabs.length - 1)]
        return query !== "" ? picker.search(query) : !chosen ? [] : chosen.group < 0 ? picker.recent : picker.group(chosen.group)
    }
    property int current: 0
    onShownEmojiChanged: current = Math.max(0, Math.min(current, shownEmoji.length - 1))
    onQueryChanged: current = 0
    onTabChanged: { current = 0; grid.positionViewAtBeginning() }
    readonly property var currentEmoji: shownEmoji[current] || null
    // The skin tones, none first: what a raised hand looks like in each.
    readonly property var tones: ["✋", "✋🏻", "✋🏼", "✋🏽", "✋🏾", "✋🏿"]
    readonly property var toneNames: ["No skin tone", "Light skin tone", "Medium-light skin tone",
                                      "Medium skin tone", "Medium-dark skin tone", "Dark skin tone"]
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start), as
    // the palette's: `progress` follows, from 0 to 1.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }

    function reset() {
        search.text = ""
        tab = 0
        current = 0
        grid.positionViewAtBeginning()
        search.forceActiveFocus()
    }
    function pick(index) {
        var emoji = shownEmoji[index]
        if (emoji)
            picker.pick(emoji.text)
    }
    function move(step) {
        if (shownEmoji.length > 0)
            current = Math.max(0, Math.min(shownEmoji.length - 1, current + step))
    }
    function key(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var columns = Math.max(1, Math.floor(grid.width / grid.cellWidth))
        if (event.key === Qt.Key_Down) {
            move(columns)
        } else if (event.key === Qt.Key_Up) {
            move(-columns)
        } else if (event.key === Qt.Key_Right && (search.text === "" || search.cursorPosition === search.length)) {
            move(1)
        } else if (event.key === Qt.Key_Left && (search.text === "" || search.cursorPosition === 0)) {
            move(-1)
        } else if (event.key === Qt.Key_Tab) {
            move(1)
        } else if (event.key === Qt.Key_Backtab) {
            move(-1)
        } else if (event.key === Qt.Key_PageDown) {
            search.text = ""
            tab = (tab + 1) % tabs.length
        } else if (event.key === Qt.Key_PageUp) {
            search.text = ""
            tab = (tab + tabs.length - 1) % tabs.length
        } else if (ctrl && event.key === Qt.Key_T) {
            picker.tone = (picker.tone + 1) % 6
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            pick(current)
        } else if (event.key === Qt.Key_Escape) {
            if (search.text !== "") search.text = ""
            else picker.close()
        } else {
            return
        }
        event.accepted = true
    }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        width: Math.min(9 * 48 + 2 * root.padding, root.screenSize.width - 32)
        height: footer.y + footer.height + root.padding
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

        SearchInput {
            id: search
            objectName: "emojiSearch"
            x: root.padding; y: root.padding
            width: card.width - 2 * x
            placeholderText: "Search emoji"
            Keys.onPressed: (event) => root.key(event)
        }
        // The tabs, the one shown underlined, and the skin tone at the end.
        Row {
            id: tabRow
            x: root.padding; y: search.y + search.height + Theme.spacingS
            spacing: Theme.spacingXS
            Repeater {
                model: root.tabs
                delegate: FlatButton {
                    id: tabButton
                    required property var modelData
                    required property int index
                    objectName: "emojiTab:" + modelData.name
                    width: 34; height: 34
                    focusPolicy: Qt.NoFocus
                    active: root.query === "" && root.tab === index
                    Accessible.name: modelData.name
                    onClicked: { search.text = ""; root.tab = index }
                    contentItem: Text {
                        text: tabButton.modelData.glyph
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 18
                    }
                }
            }
        }
        FlatButton {
            id: toneButton
            objectName: "emojiTone"
            anchors.right: parent.right; anchors.rightMargin: root.padding
            y: tabRow.y
            width: 34; height: 34
            focusPolicy: Qt.NoFocus
            Accessible.name: root.toneNames[root.picker.tone]
            onClicked: root.picker.tone = (root.picker.tone + 1) % 6
            contentItem: Text {
                text: root.tones[root.picker.tone]
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                font.pixelSize: 18
            }
        }

        GridView {
            id: grid
            objectName: "emojiGrid"
            x: root.padding; y: tabRow.y + tabRow.height + Theme.spacingS
            width: card.width - 2 * x
            height: Math.min(6 * cellHeight, Math.max(cellHeight, root.screenSize.height * 0.6 - y))
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 48)))
            cellHeight: 48
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.shownEmoji
            currentIndex: root.current
            highlightMoveDuration: 0
            highlight: Rectangle { radius: Theme.radiusSmall; color: Theme.selected }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                objectName: "emoji:" + modelData.text
                width: grid.cellWidth; height: grid.cellHeight
                Text {
                    anchors.centerIn: parent
                    text: cell.modelData.text
                    font.pixelSize: 28
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.current = cell.index
                    onClicked: root.pick(cell.index)
                }
            }
        }
        EmptyState {
            objectName: "emojiEmpty"
            visible: grid.count === 0
            x: root.padding; y: grid.y
            width: grid.width
            icon: "search"
            title: "No emoji found"
            hint: "Search by name or keyword: heart, laugh, flag"
        }
        // The name of the emoji the keyboard or the pointer is at, and the tab's.
        Text {
            id: footer
            objectName: "emojiName"
            x: root.padding; y: grid.y + grid.height + Theme.spacingS
            width: card.width - 2 * x
            text: root.currentEmoji ? root.currentEmoji.name
                : root.query === "" && root.tabs[root.tab] ? root.tabs[root.tab].name : ""
            textFormat: Text.PlainText
            elide: Text.ElideRight
            color: Theme.textMuted
            font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
        }
    }
}
