// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic

// A volume, 0 to 100, greyed while what it sets is muted. In the macOS style a pill filled with the
// accent and a white knob.
Slider {
    id: slider
    property bool muted: false
    from: 0; to: 100; stepSize: 1
    background: Rectangle {
        x: slider.leftPadding; y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth; height: Theme.sliderTrack; radius: height / 2
        color: Theme.macos ? Theme.switchTrack : Theme.selected
        Rectangle {
            // Under the knob's middle, in the macOS style.
            width: Theme.macos ? slider.handle.x - slider.leftPadding + slider.handle.width / 2
                               : slider.visualPosition * parent.width
            height: parent.height; radius: height / 2
            color: slider.muted ? Theme.textDisabled : Theme.accent
        }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: Theme.sliderKnob; height: width; radius: width / 2
        color: Theme.macos ? (slider.pressed ? Theme.mix(Theme.knob, Theme.text, 0.08) : Theme.knob)
             : slider.muted ? Theme.textMuted : (slider.pressed || slider.hovered ? Theme.accentHover : Theme.accent)
        border.color: Theme.macos ? Theme.knobOutline : "transparent"
    }
}
