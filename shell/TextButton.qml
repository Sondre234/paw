// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A button that is only its text, in the accent colour, as "Today" and "Clear all" over a card's
// list: the hover and pressed states' fill under it, greyed out while it cannot be used. A chevron
// may follow the text, pointing as `chevronRotation` turns it (90, down, for what opens under it),
// and easing round as that changes.
FlatButton {
    id: button
    property bool chevron: false
    property real chevronRotation: 90
    leftPadding: Theme.spacingM; rightPadding: chevron ? Theme.spacingS : Theme.spacingM
    Accessible.name: text
    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight
        Row {
            id: row
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingXS
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: button.text
                color: button.enabled ? Theme.accent : Theme.textDisabled
                font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold; font.family: Theme.fontFamily
            }
            Icon {
                visible: button.chevron
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron-right"; rotation: button.chevronRotation
                size: Theme.iconSizeSmall; color: Theme.accent
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
        }
    }
}
