// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The Quick Settings button, left of the clock as on Windows 11: the status of the widgets
// shell.widgets puts in Quick Settings ("quick") in small icons (the volume, the battery with its
// charge, and the network only while its link is down), there while any widget is placed there.
// Clicking opens the flyout; the wheel changes the volume and a middle click mutes it, as on the
// volume control.
FlatButton {
    id: button
    required property var panel
    objectName: "quickSettingsButton"
    readonly property var widgets: shell.widgets
    readonly property var audio: panel.audioSource
    readonly property var status: panel.statusSource
    // A link that is up is not worth the room; one that is down is a warning.
    readonly property bool showNetwork: widgets.network === "quick" && status.networkState === "disconnected"
    // The sound server the panel follows, which a test may take away as it ends.
    readonly property bool showVolume: widgets.volume === "quick" && !!audio && audio.available
    readonly property bool showBattery: widgets.battery === "quick" && status.batteryPresent
    readonly property string description: {
        var lines = []
        if (showNetwork) lines.push(status.networkText)
        if (showVolume) lines.push("Volume " + audio.volume + "%" + (audio.muted ? ", muted" : ""))
        if (showBattery) lines.push(status.batteryText)
        return lines.length > 0 ? lines.join("\n") : "Quick settings"
    }
    // The media card counts while it has a player to show, the power mode while its daemon runs,
    // Bluetooth while BlueZ has an adapter.
    readonly property bool showMedia: !!widgets.media && !!panel.mediaSource && panel.mediaSource.available
    readonly property bool showPowerMode: !!widgets.power_mode && !!panel.powerModeSource && panel.powerModeSource.available
    readonly property bool showBluetooth: !!widgets.bluetooth && !!panel.bluetoothSource && panel.bluetoothSource.available
    visible: showMedia || showPowerMode || showBluetooth ||
             ["network", "battery", "volume", "tiling", "profiles", "wallpapers", "notifications"]
                 .some(function(name) { return button.widgets[name] === "quick" })
    Layout.preferredWidth: icons.implicitWidth + 2 * Theme.spacingM
    Layout.preferredHeight: Theme.barButtonHeight
    hoverEnabled: true
    active: panel.audioPopup === "quick"
    onClicked: panel.toggleAudioPopup("quick", button)
    Accessible.name: "Quick settings: " + description
    BarTip { panel: button.panel; owner: button; text: button.description }
    contentItem: Item {
        Row {
            id: icons
            anchors.centerIn: parent
            spacing: Theme.spacingM
            Icon {
                objectName: "quickSettingsNetwork"
                visible: button.showNetwork
                anchors.verticalCenter: parent.verticalCenter
                size: Theme.iconSize
                name: "wifi-off"
                color: Theme.textMuted
            }
            SpeakerIcon {
                objectName: "quickSettingsVolume"
                visible: button.showVolume
                anchors.verticalCenter: parent.verticalCenter
                level: button.showVolume ? button.audio.volume : 0
                muted: button.showVolume && button.audio.muted
                color: muted ? Theme.textMuted : Theme.text
            }
            BatteryIcon {
                objectName: "quickSettingsBattery"
                visible: button.showBattery
                anchors.verticalCenter: parent.verticalCenter
                status: button.status
            }
            // With none of those to show, a sign that settings are behind it.
            Icon {
                objectName: "quickSettingsSliders"
                visible: !button.showNetwork && !button.showVolume && !button.showBattery
                anchors.verticalCenter: parent.verticalCenter
                name: "sliders-horizontal"
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        onClicked: if (button.showVolume) button.audio.toggleMute()
    }
    // Five percent a wheel notch, up for louder.
    WheelHandler {
        // Qt takes the whole pointer for a touchpad once the compositor offers gestures
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real travel: 0
        enabled: button.showVolume
        onWheel: (event) => {
            travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
            var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
            travel -= steps * 120
            if (steps !== 0)
                button.audio.changeVolume(steps * 5)
        }
    }
}
