// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects

// What a window dragged from the bar carries towards a workspace of the indicator: its picture,
// or its application's icon on a small card. It follows the pointer, held where it was taken
// (follow); over a workspace it docks beside it, just off the bar, smaller, gliding from one
// workspace to the next (dock); let go there, it drops into the workspace, shrinking into it and
// fading (land); let go elsewhere, it goes back where it came from and fades (back). With
// animations off it comes and goes at once.
Item {
    id: ghost
    // A picture's URL, or an icon's name in the icon theme when there is no picture.
    property url picture: ""
    property string icon: ""
    readonly property bool pictured: picture.toString() !== ""
    // Its size at full scale, and where the pointer holds it, from its top-left corner.
    property size size: Qt.size(Theme.appIconSizeLarge + 2 * Theme.spacingS, Theme.appIconSizeLarge + 2 * Theme.spacingS)
    property point hold: Qt.point(size.width / 2, size.height / 2)
    // Carried by the pointer, and docked by a workspace.
    property bool carried: false
    property bool docked: false
    readonly property real dockedScale: pictured ? 0.45 : 0.9
    readonly property real carriedScale: docked ? dockedScale : pictured ? 0.7 : 1
    width: size.width; height: size.height
    transformOrigin: Item.TopLeft
    visible: opacity > 0
    opacity: 0
    z: 100

    // Where the pointer is, and where it docks (its middle, which its owner binds), in its
    // parent's coordinates. It blends from the one to the other as it docks and back as it
    // leaves, and glides as where it docks moves.
    property real pointerX: 0
    property real pointerY: 0
    property real dockX: 0
    property real dockY: 0
    Behavior on dockX { enabled: ghost.docked && ghost.blend > 0; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    Behavior on dockY { enabled: ghost.docked && ghost.blend > 0; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    property real blend: docked ? 1 : 0
    Behavior on blend { enabled: ghost.carried; NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    readonly property real followX: pointerX - hold.x * scale
    readonly property real followY: pointerY - hold.y * scale
    readonly property real dockedX: dockX - width * scale / 2
    readonly property real dockedY: dockY - height * scale / 2
    Binding on x { when: ghost.carried; value: ghost.followX + (ghost.dockedX - ghost.followX) * ghost.blend; restoreMode: Binding.RestoreNone }
    Binding on y { when: ghost.carried; value: ghost.followY + (ghost.dockedY - ghost.followY) * ghost.blend; restoreMode: Binding.RestoreNone }

    function carry() {
        flight.stop()
        if (carried)
            return
        carried = true
        // A picture lifts off its tile where it is; an icon, which comes as the drag reaches a
        // workspace, grows in.
        if (Theme.durationFast > 0 && !pictured) {
            appear.restart()
        } else {
            grow = 1
            opacity = 1
        }
    }
    function follow(px, py) {
        pointerX = px
        pointerY = py
        docked = false
        carry()
    }
    // Docked where dockX and dockY say, the pointer at (px, py); not carried yet, it docks at
    // once.
    function dock(px, py) {
        pointerX = px
        pointerY = py
        docked = true
        carry()
    }
    // Into (x, y), shrinking away: the workspace it was let go on.
    function land(px, py) {
        flyTo(px - width * 0.05, py - height * 0.05, 0.1)
    }
    // Back to (x, y), where it was taken from, at full size.
    function back(px, py) {
        flyTo(px, py, pictured ? 1 : 0.5)
    }
    function flyTo(px, py, toScale) {
        flight.stop()
        appear.stop()
        // From the scale it has, the pointer letting go of it.
        flightScale = scale
        carried = false
        docked = false
        flyX.to = px
        flyY.to = py
        flyScale.to = toScale
        if (Theme.durationNormal === 0) {
            opacity = 0
            return
        }
        flight.start()
    }
    // Its scale: as large as being carried and docked make it, eased, times how far an icon has
    // grown in; in flight, what the flight makes it.
    property real heldScale: carried ? carriedScale : 1
    Behavior on heldScale { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }
    property real grow: 1
    property real flightScale: 1
    scale: carried ? heldScale * grow : flightScale
    ParallelAnimation {
        id: appear
        NumberAnimation { target: ghost; property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
        NumberAnimation { target: ghost; property: "grow"; from: 0.5; to: 1; duration: Theme.durationNormal; easing.type: Easing.OutBack }
    }
    ParallelAnimation {
        id: flight
        NumberAnimation { id: flyX; target: ghost; property: "x"; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { id: flyY; target: ghost; property: "y"; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { id: flyScale; target: ghost; property: "flightScale"; duration: Theme.durationNormal; easing.type: Theme.easing }
        NumberAnimation { target: ghost; property: "opacity"; to: 0; duration: Theme.durationNormal; easing.type: Theme.easingExit }
    }

    // The picture, its corners rounded and a shadow under it through the GPU.
    Item {
        anchors.fill: parent
        visible: ghost.pictured
        layer.enabled: Theme.effects && ghost.visible
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: Theme.shadow
            shadowBlur: 0.6
            shadowVerticalOffset: 4
            autoPaddingEnabled: true
        }
        Image {
            anchors.fill: parent
            source: ghost.picture
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: Theme.radiusSmall
            border.width: 2; border.color: Theme.accent
            opacity: ghost.docked ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }
    }
    // Else the icon, on a card of its own.
    Rectangle {
        anchors.fill: parent
        visible: !ghost.pictured
        radius: Theme.radiusMedium
        color: Theme.popupSurface
        border.width: ghost.docked ? 2 : 1
        border.color: ghost.docked ? Theme.accent : Theme.border
        Image {
            anchors.centerIn: parent
            readonly property real side: Math.min(Theme.appIconSizeLarge, parent.width - 2 * Theme.spacingS)
            width: side; height: side
            source: ghost.icon ? "image://icons/" + ghost.icon : ""
            sourceSize: Qt.size(Theme.appIconSizeLarge, Theme.appIconSizeLarge)
            smooth: true
        }
    }
}
