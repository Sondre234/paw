// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The clipboard history (shell.clipboard; the clipboard_history action, Super + Shift + V), as
// Windows' Win + V shows it: what was copied lately, the pinned entries first and each part
// newest first, a text by its first lines and a picture small, with a search over the texts. Up
// and Down (or Tab) choose, Enter copies the one chosen again and closes, Ctrl+P pins or unpins
// it, Delete (at the end of the search) forgets it, Escape clears the search and then closes; a
// row's buttons pin and forget it too, a click copies it, and Clear all forgets every entry but
// the pinned. A card like the command palette's, with room around it for its shadow, coming in
// each time its window shows.
Item {
    id: root
    required property size screenSize
    readonly property int padding: Theme.spacingL
    readonly property var history: shell.clipboard
    // What the search finds: the entries with every word of it in their text.
    readonly property var found: {
        var words = search.text.toLowerCase().split(/\s+/).filter(function(word) { return word !== "" })
        var entries = history.entries
        if (words.length === 0)
            return entries
        return entries.filter(function(entry) {
            var text = entry.text.toLowerCase()
            return words.every(function(word) { return text.indexOf(word) >= 0 })
        })
    }
    // The entry the keyboard is at, an index into `found`.
    property int current: 0
    onFoundChanged: current = Math.max(0, Math.min(current, found.length - 1))
    readonly property var currentEntry: found[current] || null
    // The time "5 min ago" is told from.
    property date now: new Date()
    Timer { interval: 30000; repeat: true; running: root.shown; onTriggered: root.now = new Date() }
    width: card.width + 2 * Theme.shadowMargin
    height: card.height + 2 * Theme.shadowMargin
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start), as
    // the palette's: `progress` follows, from 0 to 1.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }

    function reset() {
        search.text = ""
        current = 0
        now = new Date()
        list.positionViewAtBeginning()
        search.forceActiveFocus()
    }
    // Copies entry `index` of what is found again, closing first to hand the keyboard back.
    function choose(index) {
        var entry = found[index]
        if (!entry)
            return
        history.close()
        history.restore(entry.id)
    }
    function key(event) {
        var ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        var count = found.length
        if (event.key === Qt.Key_Down || event.key === Qt.Key_Tab) {
            if (count > 0) current = (current + 1) % count
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Backtab) {
            if (count > 0) current = (current + count - 1) % count
        } else if (event.key === Qt.Key_PageDown) {
            current = Math.max(0, Math.min(count - 1, current + 5))
        } else if (event.key === Qt.Key_PageUp) {
            current = Math.max(0, current - 5)
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            choose(current)
        } else if (ctrl && event.key === Qt.Key_P) {
            if (currentEntry) history.setPinned(currentEntry.id, !currentEntry.pinned)
        } else if (event.key === Qt.Key_Delete && search.cursorPosition === search.length) {
            if (currentEntry) history.remove(currentEntry.id)
        } else if (event.key === Qt.Key_Escape) {
            if (search.text !== "") search.text = ""
            else history.close()
        } else {
            return
        }
        event.accepted = true
    }

    Item {
        id: card
        x: Theme.shadowMargin; y: Theme.shadowMargin
        width: Math.min(560, root.screenSize.width - 32)
        height: list.y + (list.count > 0 ? list.height : empty.height) + root.padding
        opacity: root.progress
        scale: 0.97 + 0.03 * root.progress
        transform: Translate { y: (root.progress - 1) * Theme.spacingM }
        CardFrame { radius: Theme.spotlightRadius; innerEdge: true }

        SearchInput {
            id: search
            objectName: "clipboardSearch"
            x: root.padding; y: root.padding
            width: card.width - 2 * x
            placeholderText: "Search what was copied"
            Keys.onPressed: (event) => root.key(event)
        }
        // Its name, and Clear all at the end.
        Item {
            id: header
            x: root.padding; y: search.y + search.height + Theme.spacingS
            width: card.width - 2 * x; height: Theme.headingHeight
            Text {
                anchors.verticalCenter: parent.verticalCenter
                leftPadding: Theme.spacingS
                text: "Clipboard"
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold
            }
            TextButton {
                objectName: "clipboardClear"
                anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                focusPolicy: Qt.NoFocus
                text: "Clear all"
                enabled: root.history.entries.some(function(entry) { return !entry.pinned })
                onClicked: root.history.clear()
            }
        }

        ListView {
            id: list
            objectName: "clipboardList"
            x: root.padding; y: header.y + header.height + Theme.spacingXS
            width: card.width - 2 * x
            height: Math.min(contentHeight, Math.max(Theme.rowHeight, root.screenSize.height * 0.6 - y))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: Theme.spacingXS
            model: root.found
            currentIndex: root.current
            // The entry chosen is shaded as the palette's is, and moves at once.
            highlightMoveDuration: 0
            highlightResizeDuration: 0
            highlight: Rectangle { radius: Theme.radiusSmall; color: Theme.selected }
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            // An entry: its text's first lines or its picture, and in small print whether it is
            // pinned, a picture's size and when it was copied; its pin and delete buttons show
            // while it is chosen or the pointer is on it.
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                readonly property bool picture: modelData.kind === "image"
                objectName: "clipboardRow:" + modelData.id
                width: ListView.view.width
                height: body.implicitHeight + 2 * Theme.spacingM
                HoverHandler { id: hover }
                Column {
                    id: body
                    x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
                    width: buttons.x - x - Theme.spacingS
                    spacing: Theme.spacingXS
                    Text {
                        visible: !row.picture
                        width: parent.width
                        text: row.modelData.text; textFormat: Text.PlainText
                        wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    }
                    Image {
                        visible: row.picture
                        height: visible ? Math.min(96, row.modelData.height) : 0
                        width: Math.min(parent.width, height * row.modelData.width / Math.max(1, row.modelData.height))
                        fillMode: Image.PreserveAspectFit
                        source: row.modelData.image
                    }
                    Text {
                        width: parent.width
                        text: (row.modelData.pinned ? "Pinned · " : "") +
                              (row.picture ? row.modelData.width + " × " + row.modelData.height + " · " : "") +
                              shell.startMenu.ago(row.modelData.when, root.now)
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: root.current = row.index
                    onClicked: root.choose(row.index)
                }
                Row {
                    id: buttons
                    anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: hover.hovered || row.index === root.current || row.modelData.pinned ? 1 : 0
                    FlatButton {
                        objectName: "clipboardPin:" + row.modelData.id
                        width: Theme.rowHeight; height: Theme.rowHeight
                        focusPolicy: Qt.NoFocus
                        Accessible.name: row.modelData.pinned ? "Unpin" : "Pin"
                        onClicked: root.history.setPinned(row.modelData.id, !row.modelData.pinned)
                        contentItem: Item {
                            Icon {
                                anchors.centerIn: parent
                                name: row.modelData.pinned ? "pin-off" : "pin"
                                size: Theme.iconSizeSmall
                                color: row.modelData.pinned ? Theme.accent : Theme.textMuted
                            }
                        }
                    }
                    FlatButton {
                        objectName: "clipboardDelete:" + row.modelData.id
                        width: Theme.rowHeight; height: Theme.rowHeight
                        focusPolicy: Qt.NoFocus
                        Accessible.name: "Delete"
                        onClicked: root.history.remove(row.modelData.id)
                        contentItem: Item {
                            Icon { anchors.centerIn: parent; name: "trash-2"; size: Theme.iconSizeSmall; color: Theme.textMuted }
                        }
                    }
                }
            }
        }
        // While nothing is kept, or the search finds nothing.
        EmptyState {
            id: empty
            objectName: "clipboardEmpty"
            visible: list.count === 0
            x: root.padding; y: list.y
            width: card.width - 2 * root.padding
            icon: "clipboard"
            title: !root.history.enabled ? "The clipboard history is off"
                 : root.history.entries.length === 0 ? "Nothing copied yet" : "Nothing matches"
            hint: !root.history.enabled ? "shell.clipboard.enabled turns it on"
                : root.history.entries.length === 0 ? "Text and pictures you copy show here"
                : "The search looks through text"
        }
    }
}
