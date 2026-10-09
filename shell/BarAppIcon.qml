// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// An application's icon on the bar, a pinned launcher's or a window's button's, by its icon
// theme name. It shrinks a little while its button is `pressed` and eases back on release, as on
// Windows 11, so that a press shows on the icon itself.
Image {
    property string name
    property bool pressed: false
    property int size: Theme.appIconSize
    width: size; height: size
    source: "image://icons/" + name
    sourceSize: Qt.size(size, size)
    scale: pressed ? Theme.pressScale : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
}
