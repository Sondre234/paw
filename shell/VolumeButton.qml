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
    Layout.preferredWidth: level.implicitWidth + 2 * Theme.spacingM; Layout.preferredHeight: Theme.barButtonHeight
    onClicked: audioWidget.panel.toggleAudioPopup("mixer", audioWidget)
    Accessible.name: "Volume " + audioWidget.panel.audioSource.volume + "%" + (audioWidget.panel.audioSource.muted ? ", muted" : "")
    BarTip { panel: audioWidget.panel; owner: audioWidget; text: audioWidget.Accessible.name }
    active: audioWidget.panel.audioPopup === "mixer" || audioWidget.panel.audioPopup === "outputs"
    contentItem: Item {
        Row {
            id: level
            anchors.centerIn: parent; spacing: Theme.spacingS
            SpeakerIcon { anchors.verticalCenter: parent.verticalCenter; level: audioWidget.panel.audioSource.volume; muted: audioWidget.panel.audioSource.muted; color: audioWidget.panel.audioSource.muted ? Theme.textMuted : Theme.text }
            Text {
                anchors.verticalCenter: parent.verticalCenter
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
    WheelHandler {
        // Qt takes the whole pointer for a touchpad once the compositor offers gestures
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        property real travel: 0
        onWheel: (event) => {
            travel += event.angleDelta.y !== 0 ? event.angleDelta.y : -event.angleDelta.x
            var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
            travel -= steps * 120
            if (steps !== 0)
                audioWidget.panel.audioSource.changeVolume(steps * 5)
        }
    }
}
