// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import Paw

// An icon on the dock: an application's (pinned, running or both), the applications button's or
// the Trash's. It is as tall as the dock, the icon in its middle shrinking a little while pressed,
// with a dot under it while the application runs and its name on a tag above it on hover. It
// bounces (bounce()) as its application starts, until a window of it opens, and when one asks for
// attention. Coming onto the dock after it was laid out (enter()), it grows into its room.
//
// For the panel's list shown on hover and its menus it has what a taskbar button has: `stacked`,
// `groupSlot`, `groupWindowApp` and `iconName`.
Button {
    id: dockIcon
    required property var panel
    // The application's icon by its theme name, unless `tile` draws one.
    property string iconName
    property Component tile: null
    // The name on the tag.
    property string name
    // The application's windows, when it has any (a TaskFilter); `stacked` while there are several.
    property TaskFilter windows: null
    readonly property int windowCount: windows ? windows.count : 0
    readonly property bool running: windowCount > 0
    readonly property bool stacked: windowCount > 1
    readonly property bool urgent: windows !== null && windows.urgent
    property string groupSlot: ""
    property string groupWindowApp: ""
    // The windows a drag resting on it brings forward, as a taskbar button's; its name shows
    // while the drag is over it, as under the pointer.
    function dragWindows() { return windows ? windows.windows : [] }
    readonly property bool dragOver: panel.dragButton === dockIcon
    // The keyboard on the dock is at it (the panel's barKeys): a ring around its icon, and its name
    // on the tag while no list of windows is over it.
    readonly property bool keySelected: panel.barKeys.button === dockIcon && panel.barKeys.window < 0
    // How far it has come in, from 0 to 1, and how far up it is in a bounce.
    property real grow: 1
    property real lift: 0
    width: Theme.dockIconSize * grow
    height: parent ? parent.height : Theme.dockIconSize
    opacity: grow
    padding: 0
    hoverEnabled: true
    Accessible.name: name
    onHoveredChanged: if (groupSlot !== "" || groupWindowApp !== "") panel.hoverGroup(dockIcon, hovered)

    NumberAnimation on grow {
        id: growing
        running: false
        from: 0; to: 1
        duration: Theme.durationNormal; easing.type: Theme.easing
    }
    function enter() { growing.restart() }

    // Bounces `times` times, or that many from now when it is bouncing already.
    property int bouncesLeft: 0
    function bounce(times) {
        bouncesLeft = times
        if (!bounceAnimation.running)
            bounceAnimation.start()
    }
    // The bounce on its way ends, and no other follows.
    function settle() { bouncesLeft = Math.min(bouncesLeft, 1) }
    SequentialAnimation {
        id: bounceAnimation
        NumberAnimation {
            target: dockIcon; property: "lift"; from: 0; to: 1
            duration: Theme.dockBounceDuration / 2; easing.type: Easing.OutQuad
        }
        NumberAnimation {
            target: dockIcon; property: "lift"; to: 0
            duration: Theme.dockBounceDuration / 2; easing.type: Easing.InQuad
        }
        onFinished: {
            dockIcon.bouncesLeft = Math.max(0, dockIcon.bouncesLeft - 1)
            if (dockIcon.bouncesLeft > 0)
                Qt.callLater(bounceAnimation.start)
        }
    }
    readonly property bool bouncing: bounceAnimation.running || bouncesLeft > 0
    onRunningChanged: if (running) settle()
    // A window asking for attention as the dock is made has been asking a while; one that starts
    // asking later is bounced for.
    property bool made: false
    Component.onCompleted: Qt.callLater(function() { dockIcon.made = true })
    onUrgentChanged: if (urgent && made) bounce(Theme.dockBounces)

    BarTip {
        panel: dockIcon.panel; owner: dockIcon
        text: dockIcon.name
        delay: 0
        visible: !dockIcon.pressed && text.length > 0 &&
                 ((dockIcon.hovered || dockIcon.dragOver) && !dockIcon.panel.expanded ||
                  dockIcon.keySelected && !dockIcon.panel.groupOpen && !dockIcon.panel.menuOpen)
    }
    background: null
    contentItem: Item {
        BarAppIcon {
            id: image
            visible: dockIcon.tile === null
            anchors.centerIn: parent
            name: dockIcon.iconName
            pressed: dockIcon.pressed
            size: Theme.dockIconSize
            transform: Translate { y: -dockIcon.lift * Theme.dockIconSize * Theme.dockBounceHeight }
        }
        Loader {
            anchors.centerIn: parent
            width: Theme.dockIconSize; height: width
            sourceComponent: dockIcon.tile
            scale: dockIcon.pressed ? Theme.pressScale : 1
            Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
            transform: Translate { y: -dockIcon.lift * Theme.dockIconSize * Theme.dockBounceHeight }
        }
        // Rounded as an application's icon is, a little outside it.
        Item {
            anchors.centerIn: parent
            width: Theme.dockIconSize; height: width
            visible: dockIcon.keySelected
            FocusRing {
                objectName: "dockFocusRing"
                spread: Theme.focusRingWidth + 1
                radius: parent.width * 0.225 + spread
            }
        }
        // Under a running application, in the middle of the padding below its icon.
        Rectangle {
            objectName: "dockDot"
            visible: dockIcon.running
            anchors.horizontalCenter: parent.horizontalCenter
            y: parent.height - (parent.height - Theme.dockIconSize) / 4 - height / 2
            width: Theme.dockDotSize; height: width; radius: width / 2
            color: dockIcon.urgent ? Theme.urgent : Theme.text
            opacity: 0.8
        }
    }
}
