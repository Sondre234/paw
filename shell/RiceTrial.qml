// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts

// The question while a rice is on trial (`paw rice try`): "Keep this look?", the rice's name, its
// author and description, and the seconds before the previous look comes back, with Revert and
// Keep. A card in the middle of the output with nothing over the rest, so that the new look shows
// all around it, drawn in that look itself. The keyboard starts on Revert, as the display
// settings' question does, and Escape takes the rice back too.
Item {
    id: root
    objectName: "riceTrial"
    readonly property var trial: shell.riceTrial
    focus: true
    // Set by its view as it shows and cleared as it goes (a preview sets it from the start).
    property bool shown: false
    property alias progress: reveal.progress
    Reveal { id: reveal; shown: root.shown }
    readonly property int margin: Math.max(Theme.spacingL, Theme.shadowMargin)
    width: box.width + 2 * margin
    height: box.height + 2 * margin

    // What it asks about, held as it was while it goes: the trial is over before the view hears
    // that it should go.
    property string name: ""
    property string author: ""
    property string description: ""
    function hold() {
        if (!trial.active)
            return
        name = trial.name
        author = trial.author
        description = trial.description
    }
    Component.onCompleted: hold()
    Connections {
        target: root.trial
        function onChanged() { root.hold() }
    }
    function reset() { revertButton.forceActiveFocus() }
    Keys.onEscapePressed: trial.revert()
    Keys.onReturnPressed: keepButton.activeFocus ? trial.keep() : trial.revert()
    Keys.onEnterPressed: keepButton.activeFocus ? trial.keep() : trial.revert()

    Item {
        id: box
        objectName: "riceBox"
        x: root.margin
        y: root.margin
        opacity: root.progress
        scale: 0.94 + 0.06 * root.progress
        width: 420
        height: column.implicitHeight + 2 * column.anchors.margins
        CardFrame { color: Theme.surface; outline: Theme.border }
        MouseArea { anchors.fill: parent }
        ColumnLayout {
            id: column
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: Theme.spacingXXL
            spacing: Theme.spacingL
            Text {
                Layout.fillWidth: true
                text: "Keep this look?"
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeTitle; font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }
            Text {
                objectName: "riceName"
                Layout.fillWidth: true
                text: root.author === "" ? root.name : root.name + ", by " + root.author
                color: Theme.text
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize; font.weight: Font.DemiBold
                textFormat: Text.PlainText
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: root.description !== ""
                text: root.description
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
            }
            Text {
                objectName: "riceCountdown"
                Layout.fillWidth: true
                text: "The previous look comes back in " + root.trial.secondsLeft
                      + (root.trial.secondsLeft === 1 ? " second." : " seconds.")
                color: Theme.textMuted
                font.family: Theme.fontFamily; font.pixelSize: Theme.fontSize
                wrapMode: Text.Wrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacingM
                PushButton {
                    id: revertButton
                    objectName: "riceRevert"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: "Revert"
                    onClicked: root.trial.revert()
                }
                PushButton {
                    id: keepButton
                    objectName: "riceKeep"
                    Layout.minimumWidth: 3 * Theme.rowHeight
                    text: "Keep this look"
                    primary: true
                    onClicked: root.trial.keep()
                }
            }
        }
    }
}
