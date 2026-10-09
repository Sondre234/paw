// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A line icon (Icon.qml) that follows a state, as the network, the volume or do-not-disturb on
// the bar do: a new `name` crossfades from the old shape, and a new colour eases in, both on
// Theme.durationNormal (at once with animations off).
Item {
    id: fading
    property string name
    property color color: Theme.text
    property real size: Theme.iconSize
    width: size; height: size
    Behavior on color { ColorAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    // The icon shown, and the one fading out while the new one fades in ("" when none is).
    property string shown: ""
    property string leaving: ""
    property real progress: 1
    Component.onCompleted: shown = name
    onNameChanged: {
        if (shown !== "" && shown !== name) {
            leaving = shown
            crossfade.restart()
        }
        shown = name
    }
    NumberAnimation {
        id: crossfade
        target: fading; property: "progress"
        from: 0; to: 1
        duration: Theme.durationNormal; easing.type: Theme.easing
        onFinished: fading.leaving = ""
    }
    Icon {
        name: fading.name
        color: fading.color
        size: fading.size
        opacity: fading.progress
    }
    Loader {
        active: fading.leaving !== ""
        sourceComponent: Icon {
            name: fading.leaving
            color: fading.color
            size: fading.size
            opacity: 1 - fading.progress
        }
    }
}
