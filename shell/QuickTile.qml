// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// A tile of the Quick Settings flyout, as on Windows 11: a button with a line icon (`glyph`),
// filled with the accent colour while what it toggles is `checked`, and its label under it. A
// tile that opens a list (`expandable`) shows a chevron, turned while `expanded`; one that cannot
// be used now (`interactive` false, as tiling where it cannot be) is greyed out. A tile that only
// tells something (`status`, as the network's) is no button at all: its icon sits on a disc in
// the face's place, tinted with the accent colour while `checked` (connected), and it takes no
// clicks. A tile that toggles and opens a list too (`split` with `expandable`, as Wi-Fi's) has its
// chevron as a button of its own, which emits expandClicked(). In the macOS style it is a module of
// Control Center instead, its round button beside the label.
AbstractButton {
    id: tile
    property string glyph
    property string label
    // A second line under the label: the value in use, or more about the state.
    property string detail
    property bool expandable: false
    property bool expanded: false
    property bool split: false
    signal expandClicked()
    property bool interactive: true
    property bool status: false
    checkable: false
    enabled: interactive && !status
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    Accessible.role: status ? Accessible.StaticText : Accessible.Button
    Accessible.name: label + (detail ? ", " + detail : "")
    Accessible.checkable: !expandable && !status
    Accessible.checked: checked
    implicitHeight: Theme.macos ? Theme.moduleTileHeight : face.height + Theme.spacingS + caption.implicitHeight
    // Tiles in a row line up by their buttons, whether or not they have a second line.
    Layout.alignment: Qt.AlignTop
    readonly property bool on: checked && !status
    readonly property bool greyed: !interactive && !status
    // In the macOS style a module of Control Center: the label and the detail beside a round
    // button, filled with the accent while on (or, for a status, while connected).
    Rectangle {
        visible: Theme.macos
        anchors.fill: parent
        radius: Theme.moduleRadius
        color: tile.pressed ? Theme.mix(Theme.moduleColor, Theme.text, 0.1)
             : tile.hovered && tile.enabled ? Theme.mix(Theme.moduleColor, Theme.text, 0.05) : Theme.moduleColor
        border.color: Theme.moduleOutline
        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        Rectangle {
            id: disc
            readonly property bool lit: tile.on || (tile.status && tile.checked)
            x: Theme.modulePadding; anchors.verticalCenter: parent.verticalCenter
            width: Theme.moduleButtonSize; height: width; radius: width / 2
            color: lit ? Theme.accent : Theme.alpha(Theme.text, 0.1)
            Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            Icon {
                anchors.centerIn: parent
                name: tile.glyph; size: Theme.iconSizeSmall
                color: disc.lit ? Theme.textOnAccentFill : tile.greyed ? Theme.textDisabled : Theme.text
            }
        }
        Column {
            anchors.left: disc.right; anchors.leftMargin: Theme.spacingM
            anchors.right: chevron.visible ? chevron.left : parent.right
            anchors.rightMargin: chevron.visible ? Theme.spacingXS : Theme.modulePadding
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: tile.label; textFormat: Text.PlainText; elide: Text.ElideRight
                color: tile.greyed ? Theme.textDisabled : Theme.text
                font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Text {
                visible: text.length > 0
                width: parent.width
                text: tile.detail; textFormat: Text.PlainText; elide: Text.ElideRight
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
            }
        }
        Icon {
            id: chevron
            visible: tile.expandable
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"; size: Theme.iconSizeSmall; rotation: tile.expanded ? 90 : 0
            color: Theme.textMuted
            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
    }
    // The button, or for a status the place it would be.
    Rectangle {
        id: face
        visible: !Theme.macos
        width: parent.width; height: Theme.rowHeight + Theme.spacingL
        radius: Theme.radiusMedium
        color: tile.status ? "transparent"
             : tile.on ? (tile.pressed ? Theme.mix(Theme.accent, Theme.text, 0.2)
                                       : tile.hovered ? Theme.accentHover : Theme.accent)
             : tile.pressed || (tile.hovered && tile.enabled) ? Theme.surfaceRaisedHover : Theme.surfaceRaised
        border.color: tile.on || tile.status ? "transparent" : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        Rectangle {
            visible: tile.status
            anchors.centerIn: parent
            width: parent.height - Theme.spacingM; height: width; radius: width / 2
            color: tile.checked ? Theme.accentSubtle : Theme.selected
        }
        Icon {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: tile.expandable ? -Theme.spacingS : 0
            name: tile.glyph; size: Theme.iconSize
            color: tile.on ? Theme.textOnAccent : tile.greyed ? Theme.textDisabled
                 : tile.status ? (tile.checked ? Theme.accent : Theme.textMuted) : Theme.text
        }
        Icon {
            visible: tile.expandable
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingS
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron-right"; size: Theme.iconSizeSmall; rotation: tile.expanded ? 90 : 0
            color: tile.on ? Theme.textOnAccent : Theme.textMuted
            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        // Between the toggle and the chevron of a split tile.
        Rectangle {
            visible: tile.split && tile.expandable
            x: parent.width - arrow.width; anchors.verticalCenter: parent.verticalCenter
            width: 1; height: parent.height - 2 * Theme.spacingM
            color: tile.on ? Theme.alpha(Theme.textOnAccent, 0.35) : Theme.border
        }
    }
    // A split tile's chevron, which opens its list whether or not what it toggles is on.
    AbstractButton {
        id: arrow
        objectName: tile.objectName + ":arrow"
        visible: tile.split && tile.expandable
        enabled: tile.interactive
        x: tile.width - width
        width: Theme.macos ? Theme.moduleButtonSize + Theme.spacingS : Theme.rowHeight - Theme.spacingS
        height: Theme.macos ? tile.height : face.height
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.Button
        Accessible.name: tile.label + (tile.expanded ? ": hide the list" : ": show the list")
        onClicked: tile.expandClicked()
        background: Rectangle {
            topRightRadius: Theme.macos ? Theme.moduleRadius : Theme.radiusMedium
            bottomRightRadius: topRightRadius
            color: arrow.pressed ? Theme.pressed : arrow.hovered ? Theme.hover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        contentItem: Item {}
    }
    Column {
        id: caption
        visible: !Theme.macos
        y: face.height + Theme.spacingS
        width: parent.width
        Text {
            width: parent.width
            text: tile.label; textFormat: Text.PlainText; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            color: tile.greyed ? Theme.textDisabled : Theme.text
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        Text {
            visible: text.length > 0
            width: parent.width
            text: tile.detail; textFormat: Text.PlainText; elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
        }
    }
    background: Item {}
    contentItem: Item {}
}
