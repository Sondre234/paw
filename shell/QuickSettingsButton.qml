// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The Quick Settings button, left of the clock as on Windows 11: the status of the widgets
// shell.widgets puts in Quick Settings ("quick") in small icons (the volume, the battery with its
// charge, and the network only while its link is down), there while any widget is placed there.
// Clicking opens the flyout; the wheel changes the volume, a middle click mutes it and a right
// click lists the outputs, as on the volume control.
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
    Layout.preferredWidth: panel.vertical ? panel.barItemWidth : icons.implicitWidth + 2 * Theme.spacingM
    Layout.preferredHeight: panel.vertical ? icons.implicitHeight + 2 * Theme.spacingM : Theme.barButtonHeight
    hoverEnabled: true
    // The bar's own volume control is gone while the volume sits here, so the outputs are this
    // button's.
    active: panel.audioPopup === "quick" || (showVolume && panel.audioPopup === "outputs")
    onClicked: panel.toggleAudioPopup("quick", button)
    Accessible.name: "Quick settings: " + description
    BarTip { panel: button.panel; owner: button; text: button.description }
    contentItem: Item {
        // In a row, or a column along a side.
        Grid {
            id: icons
            anchors.centerIn: parent
            spacing: Theme.spacingM
            columns: panel.vertical ? 1 : 4
            horizontalItemAlignment: Grid.AlignHCenter; verticalItemAlignment: Grid.AlignVCenter
            Icon {
                objectName: "quickSettingsNetwork"
                visible: button.showNetwork
                bar: true
                size: Theme.barIconSize
                name: "wifi-off"
                color: Theme.textMuted
            }
            SpeakerIcon {
                objectName: "quickSettingsVolume"
                visible: button.showVolume
                bar: true
                size: Theme.barIconSize
                level: button.showVolume ? button.audio.volume : 0
                muted: button.showVolume && button.audio.muted
                color: muted ? Theme.textMuted : Theme.text
            }
            BatteryIcon {
                objectName: "quickSettingsBattery"
                visible: button.showBattery
                size: Theme.barIconSize
                status: button.status
            }
            // With none of those to show, a sign that settings are behind it.
            Icon {
                objectName: "quickSettingsSliders"
                visible: !button.showNetwork && !button.showVolume && !button.showBattery
                bar: true
                size: Theme.barIconSize
                name: "sliders-horizontal"
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        enabled: button.showVolume
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                button.panel.toggleAudioPopup("outputs", button)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton)
                button.audio.toggleMute()
        }
    }
    // Five percent a wheel notch, up for louder.
    NotchWheel {
        invertX: true
        enabled: button.showVolume
        onNotched: (steps) => button.audio.changeVolume(steps * 5)
    }
}
