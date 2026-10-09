// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// The display mode popup, as Windows' Win+P: the four ways to use the monitors side by side, the
// one the compositor's stepping shows selected and the one in force marked. A click takes one at
// once. It grows in and fades as the on-screen display does.
Item {
    id: popup
    required property string outputName
    readonly property var model: shell.displayModes
    readonly property bool mine: model.active && model.output === outputName
    // How far it is shown, from 0 to 1; the view hides the surface once it is back at 0.
    property real progress: 0
    readonly property bool visibleNow: mine || progress > 0
    // The room around the card, which its shadow takes when there is one.
    readonly property int margin: Math.max(Theme.spacingL, Theme.shadowMargin)
    // The names Windows gives the choices.
    readonly property var labels: ({
        "internal": qsTr("PC screen only"),
        "duplicate": qsTr("Duplicate"),
        "extend": qsTr("Extend"),
        "external": qsTr("Second screen only")
    })
    width: card.width + 2 * margin
    height: card.height + 2 * margin
    states: State {
        name: "shown"
        when: popup.mine
        PropertyChanges { popup.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationSlow; easing.type: Theme.easingExit }
        }
    ]

    Item {
        id: card
        objectName: "displayModeCard"
        anchors.centerIn: parent
        width: tiles.width + 2 * Theme.spacingL
        height: heading.height + tiles.height + 2 * Theme.spacingL + Theme.spacingM
        opacity: popup.progress
        scale: Theme.growFrom + (1 - Theme.growFrom) * popup.progress
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.radiusLarge
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.popupSurface
            border.color: Theme.popupOutline
        }
        Text {
            id: heading
            x: Theme.spacingL + Theme.spacingS; y: Theme.spacingL
            text: qsTr("Display")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily; font.weight: Font.Medium
        }
        Row {
            id: tiles
            x: Theme.spacingL; y: heading.y + heading.height + Theme.spacingM
            spacing: Theme.spacingS
            Repeater {
                model: popup.model.choices
                delegate: Item {
                    id: tile
                    required property string modelData
                    readonly property bool selected: modelData === popup.model.shown
                    objectName: "displayMode-" + modelData
                    width: 112; height: 92
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusMedium
                        color: tile.selected ? Theme.accentSubtle : hover.hovered ? Theme.hover : "transparent"
                        border.color: tile.selected ? Theme.accent : "transparent"
                        border.width: tile.selected ? 2 : 0
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        spacing: Theme.spacingM
                        // Two monitors side by side, lit where the choice shows the desktop: the
                        // first alone, both with the same picture, one picture across both, or
                        // the second alone.
                        Item {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: picture.width; height: picture.height
                            Row {
                                id: picture
                                spacing: Theme.spacingS
                                Repeater {
                                    model: 2
                                    delegate: Rectangle {
                                        required property int index
                                        readonly property bool lit: tile.modelData === "duplicate" || tile.modelData === "extend"
                                                                    || (index === 0) === (tile.modelData === "internal")
                                        width: 34; height: 22
                                        radius: 3
                                        color: lit ? Theme.accent : "transparent"
                                        border.color: lit ? Theme.accent : Theme.textMuted
                                        border.width: 2
                                        // The same mark on both while they duplicate one picture.
                                        Rectangle {
                                            visible: tile.modelData === "duplicate"
                                            anchors.centerIn: parent
                                            width: 12; height: 8; radius: 2
                                            color: Theme.textOnAccent
                                        }
                                    }
                                }
                            }
                            // One picture across both while they extend the desktop.
                            Rectangle {
                                visible: tile.modelData === "extend"
                                x: 8; y: 7
                                width: picture.width - 16; height: 8; radius: 2
                                color: Theme.textOnAccent
                            }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 2 * Theme.spacingS
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight
                            text: popup.labels[tile.modelData]
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                            font.weight: tile.modelData === popup.model.current ? Font.DemiBold : Font.Normal
                        }
                    }
                    HoverHandler { id: hover }
                    TapHandler { onTapped: popup.model.choose(tile.modelData) }
                }
            }
        }
    }
}
