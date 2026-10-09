// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The background of a button without a frame of its own (a FlatButton, a window's button on the
// bar): nothing at rest, and the hover, pressed or selected state laid over what is under it
// while the button is so. The colour eases from one state to the next on Theme.durationFast, so
// the pointer passing along the bar leaves a short fade rather than a flicker; with animations
// off it changes at once. `stateColor` is the colour the states give, for a button that tints
// itself otherwise at times (an urgent window's).
Rectangle {
    property bool hovered: false
    property bool pressed: false
    // What the button opens is open, what it toggles is on, or the keyboard is at it.
    property bool active: false
    // The states are the text colour at an opacity, so at rest it is that colour fully
    // transparent: the fade changes only the opacity, never passing through another colour.
    readonly property color stateColor: pressed ? Theme.pressed : active ? Theme.selected
                                      : hovered ? Theme.hover : Theme.alpha(Theme.hover, 0)
    radius: Theme.radiusSmall
    color: stateColor
    Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
}
