// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The network: ascending bars for Wi-Fi, a plug for a wired link, dimmed and struck through
// when the interface is down. Where NetworkManager runs, it shows the connection the machine goes
// out through, the bars following the Wi-Fi signal and the tip naming the network, and a click
// lists the Wi-Fi networks (WifiPopup) where there is a Wi-Fi device.
FlatButton {
    id: network
    required property var panel
    objectName: "networkWidget"
    readonly property var status: network.panel.statusSource
    readonly property var wifi: network.panel.wifiSource
    readonly property bool managed: !!wifi && wifi.available
    // "wifi", "ethernet", or "" while the link is down.
    readonly property string kind: !managed ? (status.networkState === "disconnected" ? "" : status.networkState)
        : wifi.primaryType === "wifi" || (wifi.primaryType === "other" && wifi.ssid !== "") ? "wifi"
        : wifi.primaryType !== "" ? "ethernet" : ""
    readonly property bool linkDown: kind === ""
    readonly property string description: !managed ? status.networkText
        : wifi.primaryType === "wifi" ? "Wi-Fi: " + wifi.primaryName + ", signal " + wifi.strength + "%"
        : wifi.primaryType === "ethernet" ? "Wired: " + wifi.primaryName
        : wifi.primaryType !== "" ? "Connected: " + wifi.primaryName
        : !wifi.hasWifi || wifi.enabled ? "Not connected" : "Wi-Fi is off"
    readonly property color tint: linkDown ? Theme.textMuted : Theme.text
    visible: shell.widgets.network === "bar" && (status.networkState !== "none" || (managed && wifi.hasWifi))
    Layout.preferredWidth: Theme.barButtonWidth; Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    active: panel.audioPopup === "wifi"
    onClicked: if (managed && wifi.hasWifi) panel.toggleAudioPopup("wifi", network)
    Accessible.name: description
    BarTip { panel: network.panel; owner: network; text: network.description }
    contentItem: Item {
        FadingIcon {
            anchors.centerIn: parent
            readonly property int strength: network.managed && network.wifi.ssid !== "" ? network.wifi.strength : 100
            name: network.kind === "ethernet" ? "ethernet-port" : network.linkDown ? "wifi-off"
                : strength >= 70 ? "wifi" : strength >= 45 ? "wifi-high" : strength >= 20 ? "wifi-low" : "wifi-zero"
            color: network.tint
        }
    }
}
