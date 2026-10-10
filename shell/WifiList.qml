// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The Wi-Fi networks in range, as Windows 11 lists them under Quick Settings' tile: each with its
// signal, a lock when it is secured and whether it is connected, the one connected first. A click
// on one opens it: Connect, or Disconnect for the one connected, and for a network that needs a
// password a field for it first (asked again, and said, when NetworkManager refuses it). A network
// that needs an enterprise login or WEP says so instead. `wifi` is the model (Wifi); while
// `shown`, the list is open, and as it opens the devices look for networks again.
// `returnFocus`, the card the list is on, takes the keyboard back as a password field closes, so
// that Escape still closes the card.
Column {
    id: list
    required property var wifi
    property bool shown: false
    property Item returnFocus: null
    objectName: "wifiList"
    // The network opened, and the one whose password was refused.
    property string selected: ""
    property string rejected: ""
    onShownChanged: {
        selected = ""
        rejected = ""
        if (shown)
            wifi.scan()
    }
    // A password field that opens takes the keyboard after this.
    onSelectedChanged: if (shown && returnFocus) returnFocus.forceActiveFocus()
    Connections {
        target: list.wifi
        // The list it was typed in asks again; another, not open, does not.
        function onPasswordRejected(ssid) {
            if (!list.shown)
                return
            list.selected = ssid
            list.rejected = ssid
        }
    }
    // Ascending bars by signal, as on the bar.
    function glyph(strength) {
        return strength >= 70 ? "wifi" : strength >= 45 ? "wifi-high" : strength >= 20 ? "wifi-low" : "wifi-zero"
    }

    // The radio off, or nothing found.
    Text {
        objectName: "wifiListEmpty"
        visible: !list.wifi.enabled || list.wifi.networks.count === 0
        width: list.width; height: Theme.rowHeight
        text: !list.wifi.hardwareEnabled ? "Wi-Fi is turned off by a switch on this computer"
            : !list.wifi.enabled ? "Wi-Fi is off" : "Looking for networks…"
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
    }
    Repeater {
        model: list.wifi.networks
        Column {
            id: entry
            required property string ssid
            required property int strength
            required property string security
            required property bool secured
            required property bool active
            required property bool connecting
            width: list.width
            readonly property bool open: list.selected === ssid
            readonly property bool asksPassword: open && list.wifi.needsPassword(ssid)
            function connectNow() {
                if (asksPassword && password.text.length < 8)
                    return
                const network = ssid, key = asksPassword ? password.text : "", wifi = list.wifi
                list.rejected = ""
                list.selected = ""
                wifi.connectTo(network, key)
            }
            FlatButton {
                id: row
                objectName: "wifiNetwork"
                width: parent.width; height: Theme.rowHeight + Theme.spacingS
                text: entry.ssid
                active: entry.open || entry.active
                leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
                focusPolicy: Qt.NoFocus
                Accessible.description: status.text
                onClicked: {
                    list.selected = entry.open ? "" : entry.ssid
                    list.rejected = ""
                }
                contentItem: RowLayout {
                    spacing: Theme.spacingM
                    Item {
                        Layout.preferredWidth: Theme.iconSize; Layout.preferredHeight: Theme.iconSize
                        Icon {
                            anchors.centerIn: parent
                            name: list.glyph(entry.strength)
                            color: entry.active ? Theme.accent : Theme.text
                        }
                        Icon {
                            visible: entry.secured
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.rightMargin: -Theme.spacingXS; anchors.bottomMargin: -Theme.spacingXS
                            name: "lock"; size: Math.round(Theme.iconSizeSmall * 0.7)
                            color: Theme.textMuted
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: entry.ssid; textFormat: Text.PlainText; elide: Text.ElideRight
                            color: Theme.text
                            font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                            font.weight: entry.active ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            id: status
                            Layout.fillWidth: true
                            text: entry.active ? (entry.secured ? "Connected, secured" : "Connected")
                                : entry.connecting ? "Connecting…"
                                : entry.security === "enterprise" ? "Secured (enterprise)"
                                : entry.secured ? "Secured" : "Open"
                            textFormat: Text.PlainText; elide: Text.ElideRight
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                        }
                    }
                }
            }
            // What can be done with the network opened.
            ColumnLayout {
                visible: entry.open
                width: parent.width
                spacing: Theme.spacingS
                TextEntry {
                    id: password
                    objectName: "wifiPassword"
                    visible: entry.asksPassword
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingM; Layout.rightMargin: Theme.spacingM
                    echoMode: TextInput.Password
                    placeholderText: "Network security key"
                    Accessible.name: "Password for " + entry.ssid
                    onVisibleChanged: {
                        text = ""
                        if (visible && list.shown)
                            Qt.callLater(function() { if (password.visible) password.forceActiveFocus() })
                    }
                    onAccepted: entry.connectNow()
                    Keys.onEscapePressed: list.selected = ""
                }
                Text {
                    objectName: "wifiPasswordRefused"
                    visible: entry.asksPassword && list.rejected === entry.ssid
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingM; Layout.rightMargin: Theme.spacingM
                    text: "The password was not accepted. Try again."
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                }
                Text {
                    visible: !entry.active && !list.wifi.canConnect(entry.ssid)
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spacingM; Layout.rightMargin: Theme.spacingM
                    text: entry.security === "enterprise"
                        ? "This network needs a sign-in; set it up with NetworkManager's own tools."
                        : "This network uses WEP, which is not safe; set it up with NetworkManager's own tools."
                    wrapMode: Text.Wrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    Layout.rightMargin: Theme.spacingM; Layout.bottomMargin: Theme.spacingS
                    spacing: Theme.spacingS
                    PushButton {
                        objectName: "wifiCancel"
                        visible: entry.asksPassword
                        small: true
                        text: "Cancel"
                        onClicked: list.selected = ""
                    }
                    PushButton {
                        objectName: "wifiConnect"
                        visible: !entry.active && list.wifi.canConnect(entry.ssid)
                        small: true; primary: true
                        // WPA keys are 8 to 63 characters.
                        enabled: !entry.asksPassword || password.text.length >= 8
                        text: entry.connecting ? "Connecting…" : "Connect"
                        onClicked: entry.connectNow()
                    }
                    PushButton {
                        objectName: "wifiDisconnect"
                        visible: entry.active
                        small: true
                        text: "Disconnect"
                        onClicked: {
                            list.wifi.disconnectNetwork()
                            list.selected = ""
                        }
                    }
                }
            }
        }
    }
}
