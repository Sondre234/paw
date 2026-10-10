// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Effects
import QtQuick.Layouts

// The confirmation of power off, restart and log out, over a dimmed screen: the action goes
// ahead when the countdown runs out, on its button or on Enter; Escape, Cancel or a click
// beside the dialog gives it up. The keyboard starts on the action's button, and Tab moves it
// to Cancel and back, Enter pressing the one it is on. The screen dims and the dialog grows in
// each time it shows.
Item {
    id: root
    objectName: "powerDialog"
    readonly property var power: shell.power
    focus: true
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    // `progress` follows, from 0 to 1: the scrim's and the dialog's opacity, and what is left of
    // the dialog's growth. It goes quicker than it came.
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }
    // What it asks about, held as it was while it goes: the question is over before the view hears
    // that it should go, so only a question still pending is taken.
    property string pending: ""
    property string pendingTitle: ""
    property string message: ""
    function hold() {
        if (power.pending === "")
            return
        pending = power.pending
        pendingTitle = power.pendingTitle
        message = power.message
    }
    Component.onCompleted: hold()
    Connections {
        target: root.power
        function onPendingChanged() { root.hold() }
        function onCountdownChanged() { root.hold() }
    }
    Keys.onEscapePressed: root.power.cancel()
    Keys.onReturnPressed: cancel.activeFocus ? root.power.cancel() : root.power.confirm()
    Keys.onEnterPressed: cancel.activeFocus ? root.power.cancel() : root.power.confirm()

    function reset() { confirm.forceActiveFocus() }

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.progress
    }
    MouseArea {
        objectName: "powerBackdrop"
        anchors.fill: parent
        onClicked: root.power.cancel()
    }
    Item {
        id: box
        objectName: "powerBox"
        anchors.centerIn: parent
        opacity: root.progress
        scale: 0.94 + 0.06 * root.progress
        width: Math.min(420, root.width - 2 * Theme.spacingXL)
        height: column.implicitHeight + 2 * column.anchors.margins
        Loader {
            anchors.fill: parent
            active: Theme.effects
            sourceComponent: RectangularShadow {
                radius: Theme.radiusLarge
                blur: Theme.shadowBlur
                offset: Qt.vector2d(0, Theme.shadowOffset)
                color: Theme.shadow
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLarge
            color: Theme.surface
            border.color: Theme.border
        }
        // A click on the dialog itself gives nothing up.
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: column
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: Theme.spacingXXL
            spacing: Theme.spacingXL
            // The action's icon on a disc of the danger colour, beside its name and what is
            // about to happen.
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingL
                Rectangle {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: Theme.rowHeight + Theme.spacingS
                    Layout.preferredHeight: Theme.rowHeight + Theme.spacingS
                    radius: width / 2
                    color: Theme.dangerSurface
                    Icon {
                        anchors.centerIn: parent
                        name: root.pending === "reboot" ? "rotate-ccw"
                              : root.pending === "logout" ? "log-out" : "power"
                        size: Theme.iconSizeLarge; color: Theme.danger
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Text {
                        Layout.fillWidth: true
                        text: root.pendingTitle
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                    }
                    Text {
                        objectName: "powerMessage"
                        Layout.fillWidth: true
                        text: root.message
                        wrapMode: Text.WordWrap
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    }
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingM
                PushButton {
                    id: cancel
                    objectName: "powerCancel"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: "Cancel"
                    onClicked: root.power.cancel()
                }
                PushButton {
                    id: confirm
                    objectName: "powerConfirm"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: root.pendingTitle
                    danger: true
                    onClicked: root.power.confirm()
                }
            }
        }
    }
}
