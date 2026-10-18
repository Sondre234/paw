// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes

// A notice at the foot of the desktop's icons (DesktopIconGrid): a line saying what was done and a
// button that takes it back (`acted`), for `timeout` milliseconds, until it is shown again with
// another line, or until its cross or hide() lets it go. It is a chip of the bar's colour, as the
// icons' names are, rimmed from the accent into the second hue (`partner`) as the band is, with a
// round badge holding its icon that wobbles as it comes and a sparkle on its corner; a ring in the
// second hue drains around the badge as its time runs out, and the pointer resting on the notice
// holds the time. It springs up into view and sinks away, and shown again in its place it bumps.
// It never takes the keyboard: the desktop's keys stay the icons', and a window's stay its own.
Item {
    id: toast
    objectName: "desktopToast"
    property color partner: Theme.accent
    property string icon: "trash-2"
    property string text
    property string actionText: "Undo"
    property string actionIcon: "rotate-ccw"
    property int timeout: 8000
    // Whether it shows, from show() until its time runs out, its cross or hide(); and how far it is
    // in view, past 1 for a moment as it springs up.
    property bool shown: false
    property real reveal: shown ? 1 : 0
    // How much of its time is left, from 1 down to 0.
    property real remaining: 1
    // Its button was clicked; a press anywhere on it, for the desktop's menus to close.
    signal acted()
    signal pressed()

    function show(line) {
        const again = shown
        text = line
        shown = true
        countdown.restart()
        badgeWobble.restart()
        if (again)
            bump.restart()
    }
    function hide() {
        shown = false
        countdown.stop()
    }

    width: pill.width
    height: pill.height
    visible: reveal > 0.001
    opacity: Math.min(1, reveal)
    Behavior on reveal {
        NumberAnimation {
            duration: Theme.duration(toast.shown ? 420 : 200)
            easing.type: toast.shown ? Easing.OutBack : Easing.InCubic
            easing.overshoot: 1.6
        }
    }
    transform: Translate { y: (1 - toast.reveal) * 22 }

    NumberAnimation {
        id: countdown
        target: toast; property: "remaining"; from: 1; to: 0
        duration: toast.timeout
        paused: running && rest.hovered
        onFinished: toast.hide()
    }

    Item {
        id: pill
        width: row.width + 2 * Theme.spacingS + Theme.spacingXS
        height: 40
        // Shown again in its place, it bumps.
        property real kick: 1
        scale: (0.9 + 0.1 * Math.min(1, toast.reveal)) * kick
        transformOrigin: Item.Bottom
        HoverHandler { id: rest }
        // Under what it holds, a press anywhere on it stays on it rather than starting a band.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
            onPressed: toast.pressed()
        }
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: Shadow {
                radius: pill.height / 2
                blur: 18
                offset: Qt.vector2d(0, 5)
                color: Qt.rgba(0, 0, 0, Theme.light ? 0.16 : 0.4)
            }
        }
        // Its rim, from the accent into its partner, as the band's.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1.5
            radius: height / 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Theme.alpha(Theme.accent, 0.9) }
                GradientStop { position: 1; color: Theme.alpha(toast.partner, 0.9) }
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.surface
        }

        Row {
            id: row
            x: Theme.spacingS + Theme.spacingXS
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingM

            // The badge, the ring of the time left around it, and its sparkle.
            Item {
                width: 30
                height: 30
                anchors.verticalCenter: parent.verticalCenter
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.alpha(toast.partner, 0.22)
                        strokeWidth: 2.5
                        fillColor: "transparent"
                        PathAngleArc { centerX: 15; centerY: 15; radiusX: 13.75; radiusY: 13.75; startAngle: 0; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: toast.partner
                        strokeWidth: 2.5
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 15; centerY: 15; radiusX: 13.75; radiusY: 13.75
                            startAngle: -90; sweepAngle: 360 * toast.remaining
                        }
                    }
                }
                Rectangle {
                    id: badge
                    anchors.centerIn: parent
                    width: 22
                    height: 22
                    radius: 11
                    color: toast.partner
                    Icon {
                        anchors.centerIn: parent
                        name: toast.icon
                        size: 13
                        color: Theme.contrast(toast.partner, "#ffffff") >= 3 ? "#ffffff" : Theme.text
                    }
                }
                Icon {
                    id: twinkle
                    name: "sparkles"
                    size: 11
                    color: Theme.accent
                    x: parent.width - width / 2 - 2
                    y: -height / 2 + 3
                }
                SequentialAnimation {
                    id: badgeWobble
                    ParallelAnimation {
                        NumberAnimation { target: badge; property: "rotation"; from: 0; to: -16; duration: Theme.duration(90); easing.type: Easing.OutQuad }
                        NumberAnimation { target: twinkle; property: "scale"; from: 0.3; to: 1.3; duration: Theme.duration(140); easing.type: Easing.OutQuad }
                    }
                    ParallelAnimation {
                        NumberAnimation { target: badge; property: "rotation"; to: 0; duration: Theme.duration(420); easing.type: Easing.OutElastic; easing.amplitude: 1.4; easing.period: 0.35 }
                        NumberAnimation { target: twinkle; property: "scale"; to: 1; duration: Theme.duration(240); easing.type: Easing.OutBack }
                    }
                }
            }

            Text {
                objectName: "desktopToastText"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 340)
                text: toast.text
                textFormat: Text.PlainText
                elide: Text.ElideMiddle
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
            }

            // The button: a pill in the accent.
            Rectangle {
                id: action
                objectName: "desktopToastAction"
                anchors.verticalCenter: parent.verticalCenter
                width: actionRow.width + 2 * Theme.spacingL
                height: 28
                radius: height / 2
                color: actionArea.pressed ? Theme.mix(Theme.accent, Theme.textOnAccent, 0.25)
                     : actionArea.containsMouse ? Theme.accentHover : Theme.accent
                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                scale: actionArea.pressed ? Theme.pressScale + 0.06 : 1
                Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.OutBack } }
                Accessible.role: Accessible.Button
                Accessible.name: toast.actionText
                Row {
                    id: actionRow
                    anchors.centerIn: parent
                    spacing: Theme.spacingS
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: toast.actionIcon
                        size: 13
                        color: Theme.textOnAccent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: toast.actionText
                        color: Theme.textOnAccent
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.DemiBold
                    }
                }
                MouseArea {
                    id: actionArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: toast.pressed()
                    onClicked: toast.acted()
                }
            }

            CloseButton {
                objectName: "desktopToastClose"
                anchors.verticalCenter: parent.verticalCenter
                size: 24
                onClicked: toast.hide()
            }
        }

        SequentialAnimation {
            id: bump
            NumberAnimation { target: pill; property: "kick"; to: 1.06; duration: Theme.duration(90); easing.type: Easing.OutQuad }
            NumberAnimation { target: pill; property: "kick"; to: 1; duration: Theme.duration(260); easing.type: Easing.OutBack }
        }
    }
}
