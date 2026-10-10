// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The wheel in notches, a mouse's or a touchpad's: its turns add up, and each whole notch of 120
// (along y, else along x, which `invertX` turns around) is told as `notched(steps, horizontal)`,
// positive up or right.
WheelHandler {
    property bool invertX: false
    // What has turned since the last whole notch.
    property real travel: 0
    signal notched(int steps, bool horizontal)
    // Qt takes the whole pointer for a touchpad once the compositor offers gestures
    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
    onWheel: (event) => {
        var horizontal = event.angleDelta.y === 0
        travel += horizontal ? (invertX ? -event.angleDelta.x : event.angleDelta.x) : event.angleDelta.y
        var steps = travel > 0 ? Math.floor(travel / 120) : Math.ceil(travel / 120)
        travel -= steps * 120
        if (steps !== 0)
            notched(steps, horizontal)
    }
}
