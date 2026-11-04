// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The name of an arrangement, a saved session, asked for where the tiling button's menu was: for
// the windows as they are with Save current arrangement…, or for one of them with Rename. A
// session's name is letters, digits, '.', '_' and '-', which the card says until what is typed is
// one; saving under a name taken replaces that session, as it says, and renaming to one is
// refused. Return or Save does it, Escape or Cancel closes it.
PopupCard {
    id: prompt
    required property var panel
    parent: panel.popupLayer
    objectName: "arrangementPrompt"
    open: panel.audioPopup === "arrangement"
    readonly property string renaming: panel.arrangementRenaming
    readonly property string name: field.text.trim()
    readonly property bool valid: shell.sessionNameValid(name)
    readonly property bool taken: name !== renaming && shell.sessions.some(function(session) {
        return session.name === prompt.name
    })
    readonly property bool acceptable: valid && name !== renaming && !(taken && renaming !== "")
    readonly property real padding: Theme.spacingL
    implicitWidth: 320
    implicitHeight: column.implicitHeight + 2 * padding
    anchorRect: panel.barAnchor(panel.audioPopupX - panel.audioPopupWidth / 2, panel.audioPopupWidth)
    side: panel.popupSide
    alignment: panel.macos ? Qt.AlignLeft : Qt.AlignHCenter
    bounds: panel.popupArea
    radius: Theme.radiusLarge
    initialFocus: field
    onOpened: {
        field.text = renaming
        field.selectAll()
    }

    function close() { panel.audioPopup = "" }
    function accept() {
        if (!acceptable)
            return
        if (renaming !== "")
            shell.renameSession(renaming, name)
        else
            shell.saveSession(name)
        close()
    }

    ColumnLayout {
        id: column
        x: prompt.padding; y: prompt.padding
        width: prompt.width - 2 * prompt.padding
        spacing: Theme.spacingM
        Text {
            Layout.fillWidth: true
            text: prompt.renaming !== "" ? "Rename " + prompt.renaming : "Save arrangement"
            elide: Text.ElideRight
            color: Theme.text; font.pixelSize: Theme.fontSizeLarge; font.weight: Font.DemiBold
            font.family: Theme.fontFamily
        }
        Text {
            Layout.fillWidth: true
            visible: prompt.renaming === ""
            text: "The open windows, where each is, and how every workspace tiles."
            wrapMode: Text.Wrap
            color: Theme.textMuted; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        TextEntry {
            id: field
            objectName: "arrangementName"
            Layout.fillWidth: true
            placeholderText: "Name"
            Accessible.name: "Arrangement name"
            onAccepted: prompt.accept()
            Keys.onEscapePressed: prompt.close()
        }
        Text {
            objectName: "arrangementHint"
            Layout.fillWidth: true
            text: prompt.name !== "" && !prompt.valid ? "Only letters, digits, '.', '_' and '-'"
                  : prompt.taken && prompt.renaming !== "" ? "An arrangement is named " + prompt.name + " already"
                  : prompt.taken ? "Replaces the arrangement saved as " + prompt.name
                  : "Letters, digits, '.', '_' and '-'"
            wrapMode: Text.Wrap
            color: prompt.name !== "" && !prompt.valid || prompt.taken && prompt.renaming !== ""
                   ? Theme.danger : Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall; font.family: Theme.fontFamily
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.spacingM
            PushButton {
                objectName: "arrangementCancel"
                text: "Cancel"
                small: true
                onClicked: prompt.close()
            }
            PushButton {
                objectName: "arrangementConfirm"
                text: prompt.renaming !== "" ? "Rename" : prompt.taken ? "Replace" : "Save"
                small: true
                primary: true
                enabled: prompt.acceptable
                opacity: enabled ? 1 : 0.4
                onClicked: prompt.accept()
            }
        }
    }
}
