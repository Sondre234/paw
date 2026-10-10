// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// How far something is in view, for one that comes in while `shown` and goes again when it is
// cleared: `progress` eases from 0 to 1 over `enterDuration` and back over `exitDuration`, at once
// with animations off. Its owner draws by it (an opacity, what is left of a slide or a growth) and
// takes it as its own `progress`: `property alias progress: reveal.progress`.
StateGroup {
    id: reveal
    property bool shown: false
    property int enterDuration: Theme.durationNormal
    property int exitDuration: Theme.durationFast
    property real progress: 0
    states: State {
        name: "shown"
        when: reveal.shown
        PropertyChanges { reveal.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: reveal.enterDuration; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: reveal.exitDuration; easing.type: Theme.easingExit }
        }
    ]
}
