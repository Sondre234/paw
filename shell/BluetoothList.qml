// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// The Bluetooth devices, under Quick Settings' tile as Windows 11 and GNOME list them: the paired
// ones with their kind, battery and whether they are connected, which a click opens to connect or
// disconnect and to forget; and "Pair a new device", which looks for the devices in range while the
// list is open, each opened by a click to pair with. What BlueZ asks while a device pairs shows over
// them: a passkey to confirm, a PIN or a number to type, or a code to type on the device.
// `bluetooth` is the model (Bluetooth); `shown` says whether the list is open, and it stops looking
// as it closes. `returnFocus`, the card the list is on, takes the keyboard back as a field closes.
Column {
    id: list
    required property var bluetooth
    property bool shown: false
    property Item returnFocus: null
    objectName: "bluetoothList"
    // The device opened, by its path.
    property string selected: ""
    onShownChanged: {
        selected = ""
        if (!shown)
            bluetooth.lookFor(false)
    }
    onSelectedChanged: if (shown && returnFocus) returnFocus.forceActiveFocus()
    // A line icon for the kind of device BlueZ names.
    function glyph(icon) {
        return icon.indexOf("headset") >= 0 || icon.indexOf("headphones") >= 0 ? "headphones"
             : icon === "audio-card" || icon.indexOf("speaker") >= 0 ? "speaker"
             : icon.indexOf("keyboard") >= 0 ? "keyboard"
             : icon.indexOf("mouse") >= 0 || icon.indexOf("tablet") >= 0 ? "mouse"
             : icon.indexOf("gaming") >= 0 || icon.indexOf("joystick") >= 0 ? "gamepad-2"
             : icon === "phone" ? "smartphone"
             : icon === "computer" ? "laptop" : "bluetooth"
    }
    component Caption: Text {
        width: parent ? parent.width : 0
        wrapMode: Text.Wrap
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
    }
    component Heading: Text {
        width: parent ? parent.width : 0; height: Theme.headingHeight
        leftPadding: Theme.spacingM
        verticalAlignment: Text.AlignVCenter
        color: Theme.textMuted
        font.pixelSize: Theme.fontSizeCaption; font.weight: Font.DemiBold; font.family: Theme.fontFamily
    }
    // A device, and what can be done with it once opened.
    component Device: Column {
        id: device
        required property string path
        required property string name
        required property string icon
        required property bool paired
        required property bool connected
        required property bool busy
        required property int battery
        width: list.width
        readonly property bool open: list.selected === path
        FlatButton {
            objectName: "bluetoothDevice"
            width: parent.width; height: Theme.rowHeight + Theme.spacingS
            text: device.name
            active: device.open
            leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
            focusPolicy: Qt.NoFocus
            Accessible.description: status.text
            onClicked: list.selected = device.open ? "" : device.path
            contentItem: RowLayout {
                spacing: Theme.spacingM
                Icon {
                    name: list.glyph(device.icon)
                    color: device.connected ? Theme.accent : Theme.text
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: device.name; textFormat: Text.PlainText; elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                        font.weight: device.connected ? Font.DemiBold : Font.Normal
                    }
                    Text {
                        id: status
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: device.busy ? (device.paired ? (device.connected ? "Disconnecting…" : "Connecting…") : "Pairing…")
                            : device.connected ? "Connected" + (device.battery >= 0 ? " · " + device.battery + "% battery" : "")
                            : device.paired ? "Not connected" : ""
                        textFormat: Text.PlainText; elide: Text.ElideRight
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeCaption; font.family: Theme.fontFamily
                    }
                }
            }
        }
        RowLayout {
            visible: device.open
            anchors.right: parent.right; anchors.rightMargin: Theme.spacingM
            height: implicitHeight + Theme.spacingS
            spacing: Theme.spacingS
            PushButton {
                objectName: "bluetoothForget"
                visible: device.paired
                small: true
                text: "Forget"
                onClicked: {
                    const path = device.path
                    list.selected = ""
                    list.bluetooth.forget(path)
                }
            }
            PushButton {
                objectName: "bluetoothConnect"
                small: true; primary: true
                enabled: !device.busy && list.bluetooth.powered
                text: !device.paired ? "Pair" : device.connected ? "Disconnect" : "Connect"
                onClicked: {
                    const path = device.path, paired = device.paired, connected = device.connected
                    list.selected = ""
                    if (!paired)
                        list.bluetooth.pair(path)
                    else if (connected)
                        list.bluetooth.disconnectDevice(path)
                    else
                        list.bluetooth.connectDevice(path)
                }
            }
        }
    }

    // What BlueZ asks while a device pairs.
    Rectangle {
        id: question
        objectName: "bluetoothRequest"
        readonly property string kind: list.bluetooth.request
        readonly property bool typed: kind === "pin" || kind === "passkey"
        visible: kind !== ""
        width: list.width
        height: questionColumn.implicitHeight + 2 * Theme.spacingM
        radius: Theme.radiusSmall
        color: Theme.accentSubtle
        onTypedChanged: {
            answer.text = ""
            if (typed && list.shown)
                Qt.callLater(function() { if (answer.visible) answer.forceActiveFocus() })
        }
        function accept() {
            if (!typed || answer.acceptableInput)
                list.bluetooth.accept(answer.text)
            if (list.returnFocus)
                list.returnFocus.forceActiveFocus()
        }
        ColumnLayout {
            id: questionColumn
            x: Theme.spacingM; y: Theme.spacingM
            width: parent.width - 2 * Theme.spacingM
            spacing: Theme.spacingS
            Text {
                objectName: "bluetoothRequestText"
                Layout.fillWidth: true
                readonly property string device: list.bluetooth.requestName
                text: question.kind === "confirm" ? "Pair with " + device + "? Check that it shows this passkey."
                    : question.kind === "authorize" ? device + " asks to pair with this computer."
                    : question.kind === "pin" ? "Type the PIN for " + device + "."
                    : question.kind === "passkey" ? "Type the passkey " + device + " shows."
                    : "Type this code on " + device + ", then press Enter there."
                textFormat: Text.PlainText; wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
            }
            Text {
                objectName: "bluetoothRequestCode"
                visible: text !== ""
                text: list.bluetooth.requestCode
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold; font.family: Theme.fontFamily
                font.letterSpacing: Theme.spacingXS
                font.features: { "tnum": 1 }
            }
            TextField {
                id: answer
                objectName: "bluetoothAnswer"
                visible: question.typed
                Layout.fillWidth: true
                implicitHeight: Theme.fieldHeight
                placeholderText: question.kind === "pin" ? "PIN" : "Passkey"
                placeholderTextColor: Theme.textMuted
                validator: question.kind === "passkey" ? passkeys : pins
                inputMethodHints: question.kind === "passkey" ? Qt.ImhDigitsOnly : Qt.ImhNone
                color: Theme.text
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                selectByMouse: true
                leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
                verticalAlignment: TextInput.AlignVCenter
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
                Accessible.name: placeholderText + " for " + list.bluetooth.requestName
                onAccepted: question.accept()
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.fieldFill
                    border.color: answer.activeFocus ? Theme.accent : Theme.border
                }
                // A passkey is a number of up to six digits; a PIN up to sixteen characters.
                IntValidator { id: passkeys; bottom: 0; top: 999999 }
                RegularExpressionValidator { id: pins; regularExpression: /.{1,16}/ }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingS
                PushButton {
                    objectName: "bluetoothReject"
                    small: true
                    text: "Cancel"
                    onClicked: {
                        list.bluetooth.reject()
                        if (list.returnFocus)
                            list.returnFocus.forceActiveFocus()
                    }
                }
                PushButton {
                    objectName: "bluetoothAccept"
                    visible: question.kind !== "display"
                    small: true; primary: true
                    enabled: !question.typed || answer.acceptableInput
                    text: question.kind === "authorize" ? "Allow" : "Pair"
                    onClicked: question.accept()
                }
            }
        }
    }
    Caption {
        objectName: "bluetoothOff"
        visible: !list.bluetooth.powered
        height: Theme.rowHeight
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        text: "Bluetooth is off"
    }
    Repeater {
        model: list.bluetooth.paired
        Device {}
    }
    Caption {
        visible: list.bluetooth.powered && list.bluetooth.paired.count === 0
        height: Theme.rowHeight
        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
        text: "No paired devices"
    }
    // Looking for devices in range to pair with.
    FlatButton {
        id: look
        objectName: "bluetoothLook"
        visible: list.bluetooth.powered
        width: list.width; height: Theme.rowHeight
        text: list.bluetooth.looking ? "Stop looking for devices" : "Pair a new device"
        leftPadding: Theme.spacingM; rightPadding: Theme.spacingM
        focusPolicy: Qt.NoFocus
        onClicked: list.bluetooth.lookFor(!list.bluetooth.looking)
        contentItem: RowLayout {
            spacing: Theme.spacingM
            Icon { name: list.bluetooth.looking ? "x" : "plus"; color: Theme.accent }
            Text {
                Layout.fillWidth: true
                text: look.text; elide: Text.ElideRight
                color: Theme.accent
                font.pixelSize: Theme.fontSize; font.family: Theme.fontFamily
            }
        }
    }
    Heading {
        visible: list.bluetooth.looking && list.bluetooth.powered
        text: list.bluetooth.found.count > 0 ? "Available devices" : "Looking for devices…"
    }
    Repeater {
        model: list.bluetooth.found
        Device {}
    }
}
