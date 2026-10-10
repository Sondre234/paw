// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The network widget's popup on the bar, where NetworkManager has Wi-Fi, as macOS's Wi-Fi menu on
// its menu bar: a switch for the radio over the networks in range (WifiList), which scroll where
// there are more than the card holds.
PopupCard {
    id: popup
    required property var panel
    parent: panel.popupLayer
    objectName: "wifiPopup"
    readonly property var wifi: panel.wifiSource
    readonly property real padding: Theme.spacingL
    open: panel.audioPopup === "wifi"
    implicitWidth: 320
    implicitHeight: 2 * padding + heading.height + Theme.spacingS + Math.min(networks.implicitHeight, 9 * Theme.rowHeight)
    // Centred below the widget, or from its left edge as macOS's menus open.
    anchorRect: panel.barAnchor(panel.audioPopupX - panel.audioPopupWidth / 2, panel.audioPopupWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    RowLayout {
        id: heading
        x: popup.padding; y: popup.padding
        width: popup.width - 2 * popup.padding; height: Theme.rowHeight
        spacing: Theme.spacingM
        Text {
            Layout.fillWidth: true
            text: "Wi-Fi"; elide: Text.ElideRight
            color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        ToggleSwitch {
            id: radio
            objectName: "wifiSwitch"
            checked: popup.wifi.enabled
            enabled: popup.wifi.hardwareEnabled
            onToggled: popup.wifi.setEnabled(checked)
            Accessible.name: "Wi-Fi"
            implicitWidth: indicator.width + leftPadding + rightPadding
        }
    }
    Flickable {
        x: popup.padding; y: heading.y + heading.height + Theme.spacingS
        width: popup.width - 2 * popup.padding
        height: popup.height - y - popup.padding
        contentHeight: networks.implicitHeight
        clip: contentHeight > height
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        WifiList {
            id: networks
            objectName: "wifiPopupList"
            width: parent.width
            wifi: popup.wifi
            shown: popup.open
            returnFocus: popup
        }
    }
}
