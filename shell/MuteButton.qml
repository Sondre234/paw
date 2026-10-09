// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// A mute toggle drawn as the speaker it silences, `size` square with a speaker `iconSize` large.
FlatButton {
    id: mute
    property int level: 0
    property bool muted: false
    property real size: 32
    property real iconSize: Theme.iconSize
    width: size; height: size
    Layout.preferredWidth: size; Layout.preferredHeight: size
    contentItem: Item { SpeakerIcon { anchors.centerIn: parent; size: mute.iconSize; level: mute.level; muted: mute.muted; color: mute.muted ? Theme.textMuted : Theme.text } }
}
