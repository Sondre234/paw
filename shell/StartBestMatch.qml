// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// The best match of the start menu's search, on a card of its own: its icon large, its name,
// what it is and what its entry says of it, and buttons for what it can do: an application's
// Open and its desktop actions, a window's Switch to, an action's Run, a calculation's Copy, a
// file's Open and Open folder, the web search's Search. `current` says the keyboard is at it, and
// `button` at which of its buttons (-1 for the card itself, 0 for Open, then the actions), which
// press() presses.
AbstractButton {
    id: card
    required property var result
    required property Item launcher
    property bool current: false
    property int button: -1
    readonly property bool app: result.kind === "app"
    // Read from its desktop entry only while it shows; a file's folder is its one action.
    readonly property var actions: visible && app && !result.configured ? shell.appActions(result.appId)
        : result.kind === "file" ? [{ action: "folder", name: "Open folder", icon: "folder-open" }] : []
    readonly property int buttons: 1 + actions.length
    function press(index) {
        if (index <= 0)
            launcher.run(result)
        else if (result.kind === "file")
            launcher.openFolder(result)
        else if (index <= actions.length)
            launcher.launchAction(result.appId, actions[index - 1].action)
    }
    objectName: "startBestMatch"
    focusPolicy: Qt.NoFocus
    hoverEnabled: true
    padding: Theme.spacingL
    implicitHeight: topPadding + bottomPadding + head.height + Theme.spacingL + buttons.height
    Accessible.name: result.title
    onClicked: launcher.run(result)
    // An application's menu on a right press.
    MouseArea {
        anchors.fill: parent
        enabled: card.app
        acceptedButtons: Qt.RightButton
        onPressed: (mouse) => card.launcher.openAppMenu(card.result, card, mouse.x, mouse.y, false)
    }
    background: Rectangle {
        radius: Theme.radiusMedium
        color: card.pressed ? Theme.mix(Theme.surfaceRaisedHover, Theme.text, 0.05)
             : card.hovered ? Theme.surfaceRaisedHover : Theme.surfaceRaised
        border.color: card.current ? Theme.accent : Theme.border
    }
    contentItem: Item {
        Item {
            id: head
            width: parent.width
            height: Math.max(image.height, about.height)
            Image {
                id: image
                width: 2 * Theme.appIconSizeLarge; height: width
                sourceSize: Qt.size(width, height)
                source: "image://icons/" + card.result.icon
            }
            Column {
                id: about
                anchors.left: image.right; anchors.leftMargin: Theme.spacingL
                anchors.right: parent.right
                anchors.verticalCenter: image.verticalCenter
                spacing: Theme.spacingXS
                Text {
                    width: parent.width
                    text: card.result.title; textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                }
                Text {
                    width: parent.width
                    text: card.app ? (card.result.genericName ? "App · " + card.result.genericName : "App")
                                   : card.result.subtitle
                    textFormat: Text.PlainText
                    elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
                Text {
                    width: parent.width
                    visible: text !== ""
                    text: card.app ? card.result.description || "" : ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                }
            }
        }
        Flow {
            id: buttons
            y: head.height + Theme.spacingL
            width: parent.width
            spacing: Theme.spacingM
            PushButton {
                objectName: "startBestOpen"
                small: true
                primary: true
                focusPolicy: Qt.NoFocus
                current: card.current && card.button === 0
                text: card.app || card.result.kind === "file" ? "Open" : card.result.kind === "window" ? "Switch to"
                    : card.result.kind === "calc" ? "Copy" : card.result.kind === "web" ? "Search" : "Run"
                onClicked: card.press(0)
            }
            Repeater {
                model: card.actions
                delegate: PushButton {
                    required property var modelData
                    required property int index
                    objectName: "startBestAction:" + modelData.action
                    small: true
                    focusPolicy: Qt.NoFocus
                    current: card.current && card.button === index + 1
                    iconName: modelData.icon
                    text: modelData.name
                    onClicked: card.press(index + 1)
                }
            }
        }
    }
}
