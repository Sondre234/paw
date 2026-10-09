// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// An application playing sound, in the volume's popups (the mixer, Quick Settings): its icon,
// its name over a slider of its volume, and a mute button drawn as the speaker it silences. A
// delegate of the sound server's streams; `audio` is the server, and `sliderName` the slider's
// objectName, for tests.
RowLayout {
    id: streamRow
    required property var audio
    required property int streamId
    required property string name
    required property string icon
    required property int volume
    required property bool muted
    property string sliderName: "streamSlider"
    height: Theme.rowHeight + Theme.spacingM
    spacing: Theme.spacingS
    Image {
        source: "image://icons/" + streamRow.icon
        sourceSize: Qt.size(2 * Theme.appIconSize, 2 * Theme.appIconSize)
        Layout.preferredWidth: Theme.appIconSize; Layout.preferredHeight: Theme.appIconSize
        Layout.leftMargin: Theme.spacingXS; Layout.rightMargin: Theme.spacingXS
    }
    ColumnLayout {
        Layout.fillWidth: true; spacing: 0
        Text {
            Layout.fillWidth: true
            text: streamRow.name; textFormat: Text.PlainText; elide: Text.ElideRight
            color: Theme.text; font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
        }
        AudioSlider {
            objectName: streamRow.sliderName
            Layout.fillWidth: true; Layout.preferredHeight: Theme.iconSize + Theme.spacingS
            value: streamRow.volume; muted: streamRow.muted
            Accessible.name: streamRow.name
            onMoved: streamRow.audio.setStreamVolume(streamRow.streamId, Math.round(value))
        }
    }
    MuteButton {
        level: streamRow.volume; muted: streamRow.muted
        Accessible.name: (muted ? "Unmute " : "Mute ") + streamRow.name
        onClicked: streamRow.audio.toggleStreamMute(streamRow.streamId)
    }
}
