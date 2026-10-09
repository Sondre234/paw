// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The start menu's list of every application from A to Z, under a heading for each letter, with
// "‹ Back" above it. Its content stays `inset` from its sides.
Item {
    id: all
    required property Item launcher
    property real inset: 0
    // The list: a {header} for each letter, then that letter's applications' records.
    readonly property var entries: {
        var list = [], letter = ""
        var apps = shell.startMenu.apps
        for (var i = 0; i < apps.length; ++i) {
            if (apps[i].letter !== letter) {
                letter = apps[i].letter
                list.push({ header: letter })
            }
            list.push(apps[i])
        }
        return list
    }
    readonly property real rowHeight: Theme.rowHeight + Theme.spacingS
    // Clicking a letter's heading shows every letter instead of the list, to jump to one: # and
    // A to Z, and any other an application's name starts with.
    property bool lettersOpen: false
    readonly property var letters: {
        var list = ["#"]
        for (var c = 65; c <= 90; ++c)
            list.push(String.fromCharCode(c))
        entries.forEach(function(entry) {
            if (entry.header !== undefined && list.indexOf(entry.header) < 0)
                list.push(entry.header)
        })
        return list
    }

    // The application the keyboard is at, as an index into `entries`; -1 for none.
    property int current: -1
    onCurrentChanged: if (current >= 0) list.positionViewAtIndex(current, ListView.Contain)
    readonly property var currentApp: current >= 0 && current < entries.length ? entries[current] : null

    // The row the keyboard is at, or null.
    function currentItem() { return current < 0 ? null : list.itemAtIndex(current) }
    function reset() {
        lettersOpen = false
        current = -1
        list.positionViewAtBeginning()
    }
    // Up and Down (Tab and Backtab too) move through the applications, past the letters' headings,
    // Page Up and Page Down a list's height. Returns whether the key moved.
    function key(event) {
        var step = event.key === Qt.Key_Down || event.key === Qt.Key_Tab ? 1
                 : event.key === Qt.Key_Up || event.key === Qt.Key_Backtab ? -1
                 : event.key === Qt.Key_PageDown ? Math.max(1, Math.floor(list.height / rowHeight))
                 : event.key === Qt.Key_PageUp ? -Math.max(1, Math.floor(list.height / rowHeight)) : 0
        if (step === 0)
            return false
        lettersOpen = false
        // From nowhere, Up goes nowhere and Down to the first application.
        var at = current < 0 && step < 0 ? -1 : Math.max(-1, Math.min(entries.length - 1, current + step))
        var direction = step > 0 ? 1 : -1
        while (at >= 0 && at < entries.length && entries[at].header !== undefined)
            at += direction
        current = at
        return true
    }
    // Shows the applications under `letter` at the top of the list.
    function jump(letter) {
        for (var i = 0; i < entries.length; ++i)
            if (entries[i].header === letter) {
                list.positionViewAtIndex(i, ListView.Beginning)
                break
            }
        lettersOpen = false
    }

    Item {
        id: heading
        x: all.inset; width: all.width - 2 * all.inset; height: Theme.rowHeight
        Text {
            x: Theme.spacingM; anchors.verticalCenter: parent.verticalCenter
            text: "All apps"
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        PushButton {
            objectName: "startBack"
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
            anchors.verticalCenter: parent.verticalCenter
            small: true
            back: true
            focusPolicy: Qt.NoFocus
            text: "Back"
            onClicked: all.launcher.allApps = false
        }
    }
    ListView {
        id: list
        objectName: "startAllList"
        x: all.inset; y: heading.height
        width: all.width - 2 * all.inset; height: all.height - y
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: all.entries
        enabled: !all.lettersOpen
        opacity: all.lettersOpen ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Item {
            id: entry
            required property var modelData
            required property int index
            readonly property bool header: modelData.header !== undefined
            width: ListView.view.width
            height: header ? Theme.headingHeight : all.rowHeight
            AbstractButton {
                id: letter
                visible: entry.header
                objectName: entry.header ? "startLetter:" + entry.modelData.header : ""
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.headingHeight; height: Theme.headingHeight
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                Accessible.name: "Letters"
                onClicked: all.lettersOpen = true
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: letter.pressed ? Theme.pressed : letter.hovered ? Theme.hover : "transparent"
                }
                contentItem: Text {
                    text: entry.header ? entry.modelData.header : ""
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                }
            }
            StartRow {
                id: appRow
                visible: !entry.header
                objectName: entry.header ? "" : "startApp:" + entry.modelData.appId
                anchors.fill: parent
                iconSize: Theme.appIconSize
                iconName: entry.header ? "" : entry.modelData.icon
                title: entry.header ? "" : entry.modelData.name
                current: all.current === entry.index
                onClicked: all.launcher.launch(entry.modelData.appId)
                onMenuRequested: (x, y) => all.launcher.openAppMenu(entry.modelData, appRow, x, y, false)
            }
        }
    }
    // Every letter in place of the list, those no application starts with greyed out; a click
    // beside them goes back to the list.
    MouseArea {
        anchors.fill: list
        visible: all.lettersOpen
        onClicked: all.lettersOpen = false
    }
    Grid {
        id: letterGrid
        objectName: "startLetters"
        x: list.x + Theme.spacingM; y: list.y + Math.max(Theme.spacingL, (list.height - height) / 2)
        width: list.width - 2 * Theme.spacingM
        columns: 7
        visible: opacity > 0
        opacity: all.lettersOpen ? 1 : 0
        scale: all.lettersOpen ? 1 : 0.95
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
        Repeater {
            model: all.letters
            delegate: AbstractButton {
                id: jump
                required property string modelData
                objectName: "startJump:" + modelData
                width: letterGrid.width / letterGrid.columns; height: Theme.rowHeight + Theme.spacingL
                enabled: all.entries.some(function(entry) { return entry.header === jump.modelData })
                focusPolicy: Qt.NoFocus
                hoverEnabled: true
                onClicked: all.jump(modelData)
                background: Rectangle {
                    radius: Theme.radiusMedium
                    color: jump.pressed ? Theme.pressed : jump.hovered ? Theme.hover : "transparent"
                }
                contentItem: Text {
                    text: jump.modelData
                    horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                    color: jump.enabled ? Theme.text : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                }
            }
        }
    }
}
