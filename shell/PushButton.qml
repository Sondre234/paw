// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A framed button with text: on the raised surface with an outline, or filled with the accent
// colour (`primary`, what a click mostly does) or the danger colour (`danger`, an action that
// cannot be taken back). An icon may come before the text (`iconName`: a line icon Icon.qml
// draws, else a theme icon's name or an image's path), and a chevron after it, or before it with
// `back`. A ring in the accent colour says that the keyboard is at it: its active focus, or
// `current` for a list that moves a selection of its own. `small` is the smaller kind, in a row of
// buttons on a card or over a list; the other is a dialog's or a notification's. Its colours
// ease between states as a FlatButton's do. In the macOS style it is macOS's push button: white,
// or lighter than the surface in a dark appearance, with regular type, and a soft ring.
Button {
    id: button
    property bool primary: false
    property bool danger: false
    property bool current: false
    property bool small: false
    property bool chevron: false
    property bool back: false
    property string iconName
    readonly property color ink: primary ? Theme.textOnAccentFill : danger ? Theme.textOnDanger : Theme.text
    hoverEnabled: true
    implicitWidth: Math.max(implicitBackgroundWidth, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: small ? implicitContentHeight + topPadding + bottomPadding : Theme.buttonHeight
    topPadding: Theme.spacingS; bottomPadding: Theme.spacingS
    leftPadding: small && (back || iconName !== "") ? Theme.spacingM : Theme.spacingL
    rightPadding: small && chevron && !back ? Theme.spacingM : Theme.spacingL
    Accessible.name: text
    background: Rectangle {
        radius: Theme.radiusSmall
        color: button.primary ? (button.hovered ? Theme.accentHover : Theme.accent)
             : button.danger ? (button.hovered ? Theme.mix(Theme.dangerFill, Theme.textOnDanger, 0.12) : Theme.dangerFill)
             : button.hovered ? Theme.buttonFaceHover : Theme.buttonFace
        border.color: button.primary || button.danger ? "transparent" : Theme.buttonOutline
        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: button.pressed ? Theme.pressed : Theme.alpha(Theme.pressed, 0)
            Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
        Rectangle {
            visible: button.current || button.activeFocus
            anchors.fill: parent; anchors.margins: -Theme.spacingXS - 1
            radius: parent.radius + Theme.spacingXS + 1
            color: "transparent"
            border.color: Theme.focusRing; border.width: Theme.focusRingWidth
        }
    }
    contentItem: Item {
        id: content
        // What the icon and the chevron take beside the text.
        readonly property real extra: (button.iconName !== "" ? Theme.iconSizeSmall + row.spacing : 0) +
                                      (button.chevron || button.back ? Theme.iconSizeSmall + row.spacing : 0)
        implicitWidth: label.implicitWidth + extra
        implicitHeight: Math.max(label.implicitHeight, Theme.iconSizeSmall)
        Row {
            id: row
            anchors.centerIn: parent
            spacing: Theme.spacingS
            layoutDirection: button.back ? Qt.RightToLeft : Qt.LeftToRight
            Item {
                visible: button.iconName !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeSmall; height: Theme.iconSizeSmall
                Icon {
                    id: glyph
                    visible: known
                    name: button.iconName
                    size: Theme.iconSizeSmall
                    color: button.ink
                }
                Image {
                    visible: !glyph.known && button.iconName !== ""
                    anchors.fill: parent
                    sourceSize: Qt.size(width, height)
                    source: !visible ? "" : button.iconName.charAt(0) === "/" ? button.iconName
                                                                             : "image://icons/" + button.iconName
                }
            }
            Text {
                id: label
                // Elided to the room the button has.
                width: Math.min(implicitWidth, content.width - content.extra)
                anchors.verticalCenter: parent.verticalCenter
                text: button.text; textFormat: Text.PlainText
                elide: Text.ElideRight
                color: button.ink
                font.pixelSize: button.small ? Theme.fontSizeSmall : Theme.fontSize
                font.weight: button.small || Theme.macos ? Font.Normal : Font.Medium
                font.family: Theme.fontFamily
            }
            Icon {
                visible: button.chevron || button.back
                anchors.verticalCenter: parent.verticalCenter
                name: button.back ? "chevron-left" : "chevron-right"
                size: Theme.iconSizeSmall
                color: Theme.textMuted
            }
        }
    }
}
