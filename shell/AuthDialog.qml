// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

// The polkit authentication dialog, over a dimmed screen: what a program asks to do (polkit's
// message, and pkexec's command), the user to answer as (a list to choose from when several may),
// the password, what went wrong, and Cancel and Authenticate. Enter answers, Escape gives the
// request up, and Up and Down choose another user. A click beside it does nothing, so that a
// stray click loses no typing. The screen dims and the dialog grows in each time it shows, and it
// shakes when a password is not accepted. In the macOS style it is laid out as macOS's: the lock
// over the title, everything centred.
Item {
    id: root
    objectName: "authDialog"
    readonly property var auth: shell.authentication
    focus: true
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    property bool shown: false
    property real progress: 0
    states: State {
        name: "shown"
        when: root.shown
        PropertyChanges { root.progress: 1 }
    }
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationNormal; easing.type: Theme.easing }
        },
        Transition {
            from: "shown"
            NumberAnimation { property: "progress"; duration: Theme.durationFast; easing.type: Theme.easingExit }
        }
    ]
    readonly property var identities: auth.identities
    readonly property bool choosing: identities.length > 1

    // A request opened: an empty field with the keyboard.
    function reset() {
        field.text = ""
        field.forceActiveFocus()
    }
    function submit() {
        if (!root.auth.checking && root.auth.open)
            root.auth.submit(field.text)
    }
    function choose(step) {
        if (root.choosing)
            root.auth.identity = (root.auth.identity + step + root.identities.length) % root.identities.length
    }
    Connections {
        target: root.auth
        function onRejected() {
            field.text = ""
            field.forceActiveFocus()
            shake.restart()
        }
    }
    Keys.onEscapePressed: root.auth.cancel()
    Keys.onUpPressed: root.choose(-1)
    Keys.onDownPressed: root.choose(1)

    Rectangle {
        anchors.fill: parent
        color: Theme.scrim
        opacity: root.progress
    }
    // Clicks beside the dialog stay here, giving nothing up.
    MouseArea { anchors.fill: parent }
    Item {
        id: box
        objectName: "authBox"
        anchors.centerIn: parent
        opacity: root.progress
        scale: 0.94 + 0.06 * root.progress
        width: Math.min(440, root.width - 2 * Theme.spacingXL)
        height: column.implicitHeight + 2 * column.anchors.margins
        transform: Translate { id: shift }
        // A few quick steps either way and back, as macOS shakes a password that was wrong.
        SequentialAnimation {
            id: shake
            loops: 1
            NumberAnimation { target: shift; property: "x"; to: -Theme.spacingL; duration: Theme.duration(50) }
            NumberAnimation { target: shift; property: "x"; to: Theme.spacingL; duration: Theme.duration(80) }
            NumberAnimation { target: shift; property: "x"; to: -Theme.spacingS; duration: Theme.duration(70) }
            NumberAnimation { target: shift; property: "x"; to: 0; duration: Theme.duration(50) }
        }
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
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: column
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: Theme.spacingXXL
            spacing: Theme.spacingL
            // The lock on a disc of the accent colour, beside the title and the message; in the
            // macOS style over them, centred.
            GridLayout {
                Layout.fillWidth: true
                columns: Theme.macos ? 1 : 2
                columnSpacing: Theme.spacingL
                rowSpacing: Theme.spacingM
                Rectangle {
                    Layout.alignment: Theme.macos ? Qt.AlignHCenter : Qt.AlignTop
                    Layout.preferredWidth: Theme.rowHeight + Theme.spacingS
                    Layout.preferredHeight: Theme.rowHeight + Theme.spacingS
                    radius: width / 2
                    color: Theme.accentSubtle
                    Icon {
                        anchors.centerIn: parent
                        name: "lock"
                        size: Theme.iconSizeLarge; color: Theme.accent
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingS
                    Text {
                        Layout.fillWidth: true
                        text: "Authentication required"
                        horizontalAlignment: Theme.macos ? Text.AlignHCenter : Text.AlignLeft
                        color: Theme.text
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                    }
                    Text {
                        objectName: "authMessage"
                        Layout.fillWidth: true
                        text: root.auth.message
                        horizontalAlignment: Theme.macos ? Text.AlignHCenter : Text.AlignLeft
                        wrapMode: Text.Wrap
                        color: Theme.textMuted
                        font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                    }
                    Text {
                        objectName: "authCommand"
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: root.auth.command
                        horizontalAlignment: Theme.macos ? Text.AlignHCenter : Text.AlignLeft
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: Theme.textMuted
                        font.family: "monospace"; font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
            // The users who may answer: one is named, several are a list to choose from.
            Text {
                objectName: "authUser"
                Layout.fillWidth: true
                visible: !root.choosing
                text: root.identities.length === 1 ? "As " + root.identities[0].label : ""
                horizontalAlignment: Theme.macos ? Text.AlignHCenter : Text.AlignLeft
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
            }
            Column {
                objectName: "authUsers"
                Layout.fillWidth: true
                visible: root.choosing
                spacing: Theme.spacingXS
                Repeater {
                    model: root.choosing ? root.identities : []
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        required property int index
                        readonly property bool chosen: index === root.auth.identity
                        objectName: "authUser" + index
                        width: parent.width
                        height: Theme.rowHeight
                        radius: Theme.radiusSmall
                        color: chosen ? Theme.selected : pointer.containsMouse ? Theme.hover : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.spacingM; anchors.rightMargin: Theme.spacingM
                            spacing: Theme.spacingM
                            // The initial on a disc, as an avatar.
                            Rectangle {
                                Layout.preferredWidth: Theme.iconSize + Theme.spacingXS
                                Layout.preferredHeight: Theme.iconSize + Theme.spacingXS
                                radius: width / 2
                                color: row.chosen ? Theme.accent : Theme.surfaceRaised
                                Text {
                                    anchors.centerIn: parent
                                    text: (row.modelData.fullName || row.modelData.name).charAt(0).toUpperCase()
                                    color: row.chosen ? Theme.textOnAccent : Theme.text
                                    font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall; font.weight: Font.DemiBold
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.label
                                elide: Text.ElideRight
                                color: Theme.text
                                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                            }
                            Icon {
                                visible: row.chosen
                                name: "check"; size: Theme.iconSizeSmall; color: Theme.accent
                            }
                        }
                        MouseArea {
                            id: pointer
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                root.auth.identity = row.index
                                field.forceActiveFocus()
                            }
                        }
                    }
                }
            }
            TextField {
                id: field
                objectName: "authField"
                Layout.fillWidth: true
                implicitHeight: Theme.fieldHeight
                leftPadding: Theme.spacingL; rightPadding: Theme.spacingL
                enabled: root.auth.open && !root.auth.checking
                echoMode: root.auth.echo ? TextInput.Normal : TextInput.Password
                placeholderText: root.auth.prompt
                color: Theme.text
                placeholderTextColor: Theme.textMuted
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                verticalAlignment: TextInput.AlignVCenter
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                onAccepted: root.submit()
                background: Rectangle {
                    radius: Theme.macos ? Theme.radiusSmall + 1 : Theme.radiusSmall
                    color: Theme.fieldFill
                    border.color: field.activeFocus ? Theme.accent : Theme.border
                }
            }
            // What went wrong, else what the helper said, else that it is checking.
            Text {
                objectName: "authStatus"
                Layout.fillWidth: true
                visible: text !== ""
                text: root.auth.error !== "" ? root.auth.error
                      : root.auth.info !== "" ? root.auth.info
                      : root.auth.checking ? "Checking…" : ""
                horizontalAlignment: Theme.macos ? Text.AlignHCenter : Text.AlignLeft
                wrapMode: Text.Wrap
                color: root.auth.error !== "" ? Theme.danger : Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSmall
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingM
                PushButton {
                    objectName: "authCancel"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: "Cancel"
                    onClicked: root.auth.cancel()
                }
                PushButton {
                    objectName: "authConfirm"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: "Authenticate"
                    primary: true
                    enabled: root.auth.open && !root.auth.checking && root.auth.identity >= 0
                    onClicked: root.submit()
                }
            }
        }
    }
}
