// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Quick Settings flyout, as on Windows 11, at the bar's right end: what is playing (a media
// player's card), tiles for what shell.widgets puts in it ("quick") and for night light, the
// volume with the outputs and the applications' volumes a click away, the screen's brightness
// where it has a backlight with the display settings window under it, and the battery along its
// foot. What sits on the bar instead keeps its own button there.
//
// In the macOS style it is Control Center: the tiles are modules two to a row, and the display
// and the sound are modules of their own under a heading, each a raised card on the flyout.
PopupCard {
    id: quick
    required property var panel
    parent: panel.popupLayer
    objectName: "quickSettings"
    open: panel.audioPopup === "quick"
    readonly property var widgets: shell.widgets
    readonly property var status: panel.statusSource
    readonly property var audio: panel.audioSource
    readonly property var backlight: panel.backlightSource
    readonly property var media: panel.mediaSource
    readonly property var powerMode: panel.powerModeSource
    readonly property var wifi: panel.wifiSource
    // NetworkManager with a Wi-Fi device: its tile stands for the network's state.
    readonly property bool wifiManaged: !!wifi && wifi.available && wifi.hasWifi
    readonly property var bluetooth: panel.bluetoothSource
    readonly property bool bluetoothShown: quick.widgets.bluetooth && !!bluetooth && bluetooth.available
    readonly property var center: shell.notifications
    // A power-profiles-daemon profile's name and icon.
    function powerModeName(profile) {
        return profile === "power-saver" ? "Power saver" : profile === "balanced" ? "Balanced"
             : profile === "performance" ? "Performance" : profile
    }
    function powerModeGlyph(profile) {
        return profile === "power-saver" ? "leaf" : profile === "performance" ? "zap" : "gauge"
    }
    // Which list is open under its tile or row: "profiles", "outputs", "mixer", "powerMode", "wifi",
    // "bluetooth", or "" for none.
    property string expanded: ""
    function toggle(list) { expanded = expanded === list ? "" : list }
    onOpened: expanded = ""
    readonly property real padding: Theme.macos ? Theme.spacingL : Theme.spacingXL
    // As wide as the clock's flyout, which it lines up with.
    implicitWidth: Theme.macos ? Theme.controlCenterWidth : 7 * (Theme.rowHeight + Theme.spacingL) + 2 * padding
    implicitHeight: content.implicitHeight + 2 * padding + (footer.visible ? footer.height : 0)
    anchorRect: panel.barAnchor(panel.statusBar.x + panel.statusBar.width, 0)
    side: panel.popupSide
    alignment: Qt.AlignRight
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    color: Theme.controlCenterSurface
    readonly property string outputName: {
        var outputs = audio.outputs
        for (var i = 0; i < outputs.length; ++i)
            if (outputs[i].name === audio.output) return outputs[i].description
        return "No output"
    }

    // A row's chevron that opens or closes a list under it.
    component Expander: FlatButton {
        id: expander
        property bool expanded: false
        Layout.preferredWidth: Theme.rowHeight; Layout.preferredHeight: Theme.rowHeight
        active: expanded
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: "chevron-right"; size: Theme.iconSizeSmall; color: Theme.textMuted
                rotation: expander.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            }
        }
    }

    // A module of Control Center: a raised card around `section`, and `heading` over it, under the
    // content; none in the taskbar style.
    component Module: Rectangle {
        required property Item section
        property string heading
        readonly property real inset: Theme.modulePadding
        readonly property real headingRoom: heading !== "" ? Theme.moduleHeadingHeight : 0
        visible: Theme.macos && section.visible
        x: content.x + section.x - inset
        y: content.y + section.y - inset - headingRoom
        width: section.width + 2 * inset
        height: section.height + 2 * inset + headingRoom
        radius: Theme.moduleRadius
        color: Theme.moduleColor
        border.color: Theme.moduleOutline
        Text {
            x: parent.inset; y: parent.inset - Theme.spacingXS
            height: Theme.moduleHeadingHeight
            verticalAlignment: Text.AlignVCenter
            text: parent.heading
            color: Theme.text
            font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
    }

    // What does not fit scrolls, above the foot.
    Flickable {
        anchors.fill: parent
        anchors.bottomMargin: footer.visible ? footer.height : 0
        contentHeight: content.implicitHeight + 2 * quick.padding
        clip: contentHeight > height
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        Module { section: mediaCard }
        Module { section: profileList }
        Module { section: powerModeList }
        Module { section: wifiList }
        Module { section: bluetoothList }
        Module { section: display; heading: "Display" }
        Module { section: sound; heading: "Sound" }
        ColumnLayout {
            id: content
            // Room for the modules' cards around what they hold.
            readonly property real inset: Theme.macos ? Theme.modulePadding : 0
            x: quick.padding + inset; y: quick.padding
            width: quick.width - 2 * x
            spacing: Theme.macos ? 2 * inset + Theme.spacingM : Theme.spacingL
            // What is playing, while a player is there.
            MediaCard {
                id: mediaCard
                visible: quick.widgets.media && !!quick.media && quick.media.available
                Layout.fillWidth: true
                // Its module's card reaches out by the inset; the tiles under it are cards
                // themselves.
                Layout.topMargin: Theme.macos ? content.inset : 0
                Layout.bottomMargin: Theme.macos ? -content.inset : 0
                media: quick.media
                shown: quick.open
            }
            GridLayout {
                id: tiles
                Layout.fillWidth: true
                // The modules reach out to the cards' edges.
                Layout.leftMargin: -content.inset; Layout.rightMargin: -content.inset
                Layout.bottomMargin: -content.inset
                columns: Theme.macos ? 2 : 3
                columnSpacing: Theme.spacingM; rowSpacing: Theme.macos ? Theme.spacingM : Theme.spacingL
                // Wi-Fi, as on Windows 11: the tile turns the radio on and off, its chevron lists
                // the networks.
                QuickTile {
                    objectName: "quickTile:wifi"
                    visible: quick.widgets.network === "quick" && quick.wifiManaged
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: !quick.wifi.enabled ? "wifi-off"
                         : Theme.wifiIcon(quick.wifi.ssid === "" ? 100 : quick.wifi.strength)
                    label: "Wi-Fi"
                    detail: !quick.wifi.hardwareEnabled ? "Off by a switch" : !quick.wifi.enabled ? "Off"
                          : quick.wifi.connecting !== "" ? "Connecting…"
                          : quick.wifi.ssid !== "" ? quick.wifi.ssid : "Not connected"
                    checked: quick.wifi.enabled
                    interactive: quick.wifi.hardwareEnabled
                    expandable: true; split: true
                    expanded: quick.expanded === "wifi"
                    onClicked: quick.wifi.setEnabled(!quick.wifi.enabled)
                    onExpandClicked: quick.toggle("wifi")
                }
                // Bluetooth: the tile turns the adapter on and off, its chevron lists the devices.
                QuickTile {
                    objectName: "quickTile:bluetooth"
                    visible: quick.bluetoothShown
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    readonly property int connected: quick.bluetooth.connectedCount
                    glyph: !quick.bluetooth.powered ? "bluetooth-off" : connected > 0 ? "bluetooth-connected" : "bluetooth"
                    label: "Bluetooth"
                    detail: !quick.bluetooth.powered ? "Off" : connected > 1 ? connected + " devices"
                          : connected === 1 ? quick.bluetooth.connectedName : "Not connected"
                    checked: quick.bluetooth.powered
                    expandable: true; split: true
                    expanded: quick.expanded === "bluetooth"
                    onClicked: quick.bluetooth.setPowered(!quick.bluetooth.powered)
                    onExpandClicked: quick.toggle("bluetooth")
                }
                QuickTile {
                    objectName: "quickTile:dnd"
                    visible: quick.widgets.notifications === "quick" && quick.center.serving
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: checked ? "bell-off" : "bell"
                    label: "Do not disturb"
                    checked: quick.center.dnd
                    onClicked: quick.center.toggleDnd()
                }
                QuickTile {
                    objectName: "quickTile:nightLight"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "moon"
                    label: "Night light"
                    detail: shell.nightLightMode === "auto" ? "Scheduled" : ""
                    checked: shell.nightLight
                    onClicked: shell.send("night_light_toggle")
                }
                QuickTile {
                    objectName: "quickTile:tiling"
                    visible: quick.widgets.tiling === "quick"
                    interactive: shell.tilingAvailable
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "layout-panel-left"
                    label: "Tiling"
                    detail: "This monitor"
                    checked: quick.panel.tiling
                    onClicked: shell.toggleTiling(quick.panel.outputName)
                }
                QuickTile {
                    objectName: "quickTile:profiles"
                    visible: quick.widgets.profiles === "quick" && shell.profiles.length > 1
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "palette"
                    label: "Appearance"
                    detail: shell.profile
                    expandable: true
                    expanded: quick.expanded === "profiles"
                    onClicked: quick.toggle("profiles")
                }
                QuickTile {
                    objectName: "quickTile:wallpapers"
                    visible: quick.widgets.wallpapers === "quick"
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: "image"
                    label: "Wallpaper"
                    // The bar's own picker, by the Quick Settings button.
                    onClicked: quick.panel.toggleAudioPopup("wallpapers", quick.panel.quickSettingsButton)
                }
                QuickTile {
                    objectName: "quickTile:network"
                    visible: quick.widgets.network === "quick" && quick.status.networkState !== "none" && !quick.wifiManaged
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    // Only the state: paw does not manage connections.
                    status: true
                    glyph: quick.status.networkState === "ethernet" ? "ethernet-port"
                        : quick.status.networkState === "wifi" ? "wifi" : "wifi-off"
                    label: quick.status.networkState === "ethernet" ? "Ethernet"
                         : quick.status.networkState === "wifi" ? "Wi-Fi" : "Disconnected"
                    // NetworkManager's name for the connection, where it runs.
                    detail: !!quick.wifi && quick.wifi.available && quick.wifi.primaryName !== "" ? quick.wifi.primaryName
                          : quick.status.networkInterface
                    checked: quick.status.networkState === "ethernet" || quick.status.networkState === "wifi"
                }
                QuickTile {
                    objectName: "quickTile:powerMode"
                    visible: quick.widgets.power_mode && !!quick.powerMode && quick.powerMode.available
                    Layout.fillWidth: true; Layout.preferredWidth: 1
                    glyph: quick.powerModeGlyph(quick.powerMode.profile)
                    label: "Power mode"
                    detail: quick.powerModeName(quick.powerMode.profile)
                    // Lit while away from the daemon's default.
                    checked: quick.powerMode.profile !== "balanced"
                    expandable: true
                    expanded: quick.expanded === "powerMode"
                    onClicked: quick.toggle("powerMode")
                }
            }
            // The Wi-Fi networks, under their tile.
            WifiList {
                id: wifiList
                objectName: "quickWifiList"
                visible: quick.expanded === "wifi" && quick.wifiManaged
                shown: visible && quick.open
                returnFocus: quick
                Layout.fillWidth: true
                wifi: quick.wifi
            }
            // The Bluetooth devices, under their tile.
            BluetoothList {
                id: bluetoothList
                objectName: "quickBluetoothList"
                visible: quick.expanded === "bluetooth" && quick.bluetoothShown
                shown: visible && quick.open
                returnFocus: quick
                Layout.fillWidth: true
                bluetooth: quick.bluetooth
            }
            // The power modes, under their tile; performance says when the daemon holds it back.
            Column {
                id: powerModeList
                objectName: "quickPowerModes"
                visible: quick.expanded === "powerMode" && quick.powerMode.available
                Layout.fillWidth: true
                Repeater {
                    model: quick.powerMode.profiles.map(function(profile) {
                        var degraded = quick.powerMode.degraded
                        return { text: quick.powerModeName(profile), profile: profile, toggle: "radio",
                                 checked: profile === quick.powerMode.profile,
                                 secondary: profile !== "performance" || degraded === "" ? ""
                                          : degraded === "lap-detected" ? "Limited on a lap"
                                          : degraded === "high-operating-temperature" ? "Limited while hot" : "Limited" }
                    })
                    MenuRow {
                        objectName: "quickPowerModeItem"
                        width: parent.width
                        iconColumn: true
                        onClicked: quick.powerMode.setProfile(modelData.profile)
                    }
                }
            }
            // The appearance profiles, under their tile.
            Column {
                id: profileList
                objectName: "quickProfiles"
                visible: quick.expanded === "profiles"
                Layout.fillWidth: true
                Repeater {
                    model: shell.profiles.map(function(name) {
                        return { text: name, toggle: "radio", checked: name === shell.profile }
                    })
                    MenuRow {
                        objectName: "quickProfileItem"
                        width: parent.width
                        iconColumn: true
                        onClicked: if (modelData.text !== shell.profile) shell.pickProfile(modelData.text)
                    }
                }
            }
            // The screen's brightness, where it has a backlight, and the display settings window.
            ColumnLayout {
                id: display
                Layout.fillWidth: true
                Layout.topMargin: Theme.macos ? Theme.moduleHeadingHeight : 0
                spacing: Theme.spacingXS
                RowLayout {
                    id: brightness
                    objectName: "quickBrightness"
                    visible: quick.backlight.present
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Item {
                        Layout.preferredWidth: Theme.rowHeight - Theme.spacingS; Layout.preferredHeight: Theme.rowHeight - Theme.spacingS
                        Icon { anchors.centerIn: parent; name: "sun" }
                    }
                    AudioSlider {
                        objectName: "quickBrightnessSlider"
                        Layout.fillWidth: true
                        value: Math.max(0, quick.backlight.percent)
                        Accessible.name: "Brightness"
                        onMoved: quick.backlight.setPercent(Math.round(value))
                    }
                    Text {
                        visible: !Theme.macos
                        Layout.preferredWidth: Theme.rowHeight
                        // Level with the volume's percentage, whose row has a chevron after it.
                        Layout.rightMargin: Theme.rowHeight + Theme.spacingS
                        text: quick.backlight.percent + "%"; horizontalAlignment: Text.AlignRight
                        color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                }
                // Opens the display settings window on this monitor, closing the flyout.
                FlatButton {
                    id: displaySettings
                    objectName: "quickDisplaySettings"
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.rowHeight
                    leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                    onClicked: {
                        quick.panel.closeMenus()
                        shell.displaySettings.show(quick.panel.outputName)
                    }
                    Accessible.name: "Display settings"
                    contentItem: RowLayout {
                        spacing: Theme.spacingS
                        Item {
                            Layout.preferredWidth: Theme.rowHeight - Theme.spacingS - displaySettings.leftPadding
                            Layout.preferredHeight: Theme.iconSize
                            Icon { anchors.centerIn: parent; name: "monitor"; visible: !Theme.macos }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: Theme.macos ? "Display Settings…" : "Display settings…"; elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        }
                        Icon {
                            visible: !Theme.macos
                            name: "chevron-right"; size: Theme.iconSizeSmall; color: Theme.textMuted
                        }
                    }
                }
            }
            // The default output's volume, the outputs to play through and each application's
            // volume.
            ColumnLayout {
                id: sound
                objectName: "quickSound"
                visible: quick.widgets.volume === "quick" && quick.audio.available
                Layout.fillWidth: true
                Layout.topMargin: Theme.macos ? Theme.moduleHeadingHeight : 0
                spacing: Theme.spacingXS
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    MuteButton {
                        objectName: "quickMute"
                        level: quick.audio.volume; muted: quick.audio.muted
                        Accessible.name: muted ? "Unmute" : "Mute"
                        onClicked: quick.audio.toggleMute()
                    }
                    AudioSlider {
                        objectName: "quickVolumeSlider"
                        Layout.fillWidth: true
                        value: quick.audio.volume; muted: quick.audio.muted
                        Accessible.name: "Volume"
                        onMoved: quick.audio.setVolume(Math.round(value))
                    }
                    Text {
                        visible: !Theme.macos
                        Layout.preferredWidth: Theme.rowHeight
                        text: quick.audio.volume + "%"; horizontalAlignment: Text.AlignRight
                        color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                    Expander {
                        objectName: "quickOutputsToggle"
                        expanded: quick.expanded === "outputs"
                        Accessible.name: "Output: " + quick.outputName
                        onClicked: quick.toggle("outputs")
                    }
                }
                // The outputs, the one in use marked.
                Column {
                    objectName: "quickOutputs"
                    visible: quick.expanded === "outputs"
                    Layout.fillWidth: true
                    Repeater {
                        model: [{ header: "Output" }].concat(quick.audio.outputs.map(function(output) {
                            return { text: output.description, name: output.name, toggle: "radio",
                                     checked: output.name === quick.audio.output }
                        }))
                        MenuRow {
                            objectName: modelData.header ? "quickOutputsHeading" : "quickOutputItem"
                            width: parent.width
                            iconColumn: true
                            onClicked: quick.audio.setOutput(modelData.name)
                        }
                    }
                }
                // The applications playing sound, and their volumes under it.
                FlatButton {
                    id: mixerToggle
                    objectName: "quickMixerToggle"
                    Layout.fillWidth: true; Layout.preferredHeight: Theme.rowHeight
                    leftPadding: Theme.spacingS; rightPadding: Theme.spacingS
                    active: quick.expanded === "mixer"
                    onClicked: quick.toggle("mixer")
                    Accessible.name: "Applications"
                    contentItem: RowLayout {
                        spacing: Theme.spacingS
                        Text {
                            Layout.fillWidth: true
                            text: "Applications"; elide: Text.ElideRight
                            color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                        }
                        Text {
                            text: quick.audio.streams.count > 0 ? quick.audio.streams.count : "None playing"
                            color: Theme.textMuted; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                        }
                        Icon {
                            name: "chevron-right"; size: Theme.iconSizeSmall; color: Theme.textMuted
                            rotation: mixerToggle.active ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        }
                    }
                }
                Column {
                    objectName: "quickStreams"
                    visible: quick.expanded === "mixer"
                    Layout.fillWidth: true
                    Text {
                        visible: quick.audio.streams.count === 0
                        width: parent.width; height: Theme.rowHeight
                        text: "No applications are playing sound"
                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                        color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
                    }
                    Repeater {
                        model: quick.audio.streams
                        StreamRow {
                            width: parent.width
                            audio: quick.audio
                            sliderName: "quickStreamSlider"
                        }
                    }
                }
            }
        }
    }
    // The battery, along the foot of the card.
    Rectangle {
        id: footer
        objectName: "quickBattery"
        visible: quick.widgets.battery === "quick" && quick.status.batteryPresent
        // Inside the card's outline.
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.margins: 1
        height: Theme.rowHeight + Theme.spacingL
        bottomLeftRadius: quick.radius - 1; bottomRightRadius: quick.radius - 1
        color: Theme.macos ? "transparent" : Theme.surfaceRaised
        Rectangle { width: parent.width; height: 1; color: Theme.divider }
        Row {
            anchors.left: parent.left; anchors.leftMargin: quick.padding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingM
            BatteryIcon { anchors.verticalCenter: parent.verticalCenter; status: quick.status }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: quick.status.batteryPercent + "%" +
                      (quick.status.batteryState === "charging" ? " · Charging"
                       : quick.status.batteryState === "full" ? " · Full" : "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
    }
}
