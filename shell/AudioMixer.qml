// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The volume control's popup: the default output's volume under its name, then a slider for each
// application playing sound, as Quick Settings lists them.
PopupCard {
    id: audioMixer
    required property var panel
    required property Item barItem
    parent: panel.popupLayer
    objectName: "audioMixer"
    readonly property real rowHeight: Theme.rowHeight + Theme.spacingM
    readonly property real padding: Theme.spacingXL
    readonly property string outputName: {
        var outputs = panel.audioSource.outputs
        for (var i = 0; i < outputs.length; ++i)
            if (outputs[i].name === panel.audioSource.output) return outputs[i].description
        return "No output"
    }
    open: panel.audioPopup === "mixer"
    implicitWidth: 340
    implicitHeight: 2 * padding + 2 * Theme.headingHeight + (1 + Math.max(1, streamList.count)) * rowHeight
    anchorRect: panel.barAnchor(panel.audioPopupX, 0)
    side: panel.popupSide
    radius: Theme.radiusLarge
    ColumnLayout {
        anchors.fill: parent; anchors.margins: audioMixer.padding; spacing: 0
        Text {
            Layout.fillWidth: true; Layout.preferredHeight: Theme.headingHeight
            text: audioMixer.outputName; elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            color: Theme.text; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold; font.family: Theme.fontFamily
        }
        RowLayout {
            Layout.fillWidth: true; Layout.preferredHeight: audioMixer.rowHeight
            spacing: Theme.spacingS
            MuteButton {
                objectName: "audioMute"
                level: panel.audioSource.volume; muted: panel.audioSource.muted
                Accessible.name: muted ? "Unmute" : "Mute"
                onClicked: panel.audioSource.toggleMute()
            }
            AudioSlider {
                objectName: "audioVolumeSlider"
                Layout.fillWidth: true
                value: panel.audioSource.volume; muted: panel.audioSource.muted
                Accessible.name: "Volume"
                onMoved: panel.audioSource.setVolume(Math.round(value))
            }
            Text {
                Layout.preferredWidth: Theme.rowHeight
                text: panel.audioSource.volume + "%"; horizontalAlignment: Text.AlignRight
                color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
        Text {
            Layout.fillWidth: true; Layout.preferredHeight: Theme.headingHeight
            text: "Applications"; verticalAlignment: Text.AlignVCenter
            color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        ListView {
            id: streamList
            objectName: "audioStreams"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: panel.audioSource.streams
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: StreamRow {
                width: ListView.view.width
                audio: audioMixer.panel.audioSource
                sliderName: "audioStreamSlider"
            }
            Text {
                anchors.centerIn: parent
                visible: streamList.count === 0
                text: "No applications are playing sound"
                color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
    }
}
