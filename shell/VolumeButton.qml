// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The default output's volume. Left-click: per-application volumes; right-click: the
// output; wheel: louder or quieter; middle-click: mute.
FlatButton {
    id: audioWidget
    required property var panel
    objectName: "audioWidget"
    visible: audioWidget.panel.audioSource.available && shell.widgets.volume === "bar"
    Layout.preferredWidth: Theme.barVertical ? Theme.barItemWidth : level.implicitWidth + 2 * Theme.spacingM
    Layout.preferredHeight: Theme.barVertical ? level.implicitHeight + 2 * Theme.spacingS : Theme.barButtonHeight
    onClicked: audioWidget.panel.toggleAudioPopup("mixer", audioWidget)
    Accessible.name: "Volume " + audioWidget.panel.audioSource.volume + "%" + (audioWidget.panel.audioSource.muted ? ", muted" : "")
    BarTip { panel: audioWidget.panel; owner: audioWidget; text: audioWidget.Accessible.name }
    active: audioWidget.panel.audioPopup === "mixer" || audioWidget.panel.audioPopup === "outputs"
    contentItem: Item {
        // The level beside the speaker, or under it along a side.
        Grid {
            id: level
            anchors.centerIn: parent; spacing: Theme.spacingS
            columns: Theme.barVertical ? 1 : 2
            horizontalItemAlignment: Grid.AlignHCenter; verticalItemAlignment: Grid.AlignVCenter
            SpeakerIcon { level: audioWidget.panel.audioSource.volume; muted: audioWidget.panel.audioSource.muted; color: audioWidget.panel.audioSource.muted ? Theme.textMuted : Theme.text }
            Text {
                text: audioWidget.panel.audioSource.volume + "%"
                color: audioWidget.panel.audioSource.muted ? Theme.textMuted : Theme.text
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
        }
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton | Qt.MiddleButton
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton)
                audioWidget.panel.toggleAudioPopup("outputs", audioWidget)
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.MiddleButton) audioWidget.panel.audioSource.toggleMute()
        }
    }
    // Five percent a wheel notch, up for louder.
    NotchWheel {
        invertX: true
        onNotched: (steps) => audioWidget.panel.audioSource.changeVolume(steps * 5)
    }
}
